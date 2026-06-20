#include "net/timer.h"

#include "comm/log.h"
#include "net/reactor.h"

#include <algorithm>
#include <cerrno>
#include <chrono>
#include <cstring>
#include <string>
#include <sys/epoll.h>
#include <sys/timerfd.h>
#include <unistd.h>
#include <utility>

namespace tinyrpc {

int64_t getNowMs()
{
    auto now = std::chrono::steady_clock::now();
    auto duration = now.time_since_epoch();
    return std::chrono::duration_cast<std::chrono::milliseconds>(duration).count();
}

TimerTask::TimerTask(int64_t intervalMs, bool repeated, Callback callback)
    : m_intervalMs(normalizeInterval(intervalMs)),
      m_repeated(repeated),
      m_callback(std::move(callback))
{
    resetTime(m_intervalMs);
}

int64_t TimerTask::getIntervalMs() const
{
    return m_intervalMs;
}

int64_t TimerTask::getExpireTimeMs() const
{
    return m_expireTimeMs;
}

bool TimerTask::isRepeated() const
{
    return m_repeated;
}

bool TimerTask::isCanceled() const
{
    return m_canceled.load();
}

bool TimerTask::isExpired(int64_t nowMs) const
{
    return !m_canceled && nowMs >= m_expireTimeMs;
}

void TimerTask::cancel()
{
    m_canceled.store(true);
}

void TimerTask::resetTime()
{
    resetTime(m_intervalMs);
}

void TimerTask::resetTime(int64_t intervalMs)
{
    m_intervalMs = normalizeInterval(intervalMs);
    m_expireTimeMs = getNowMs() + m_intervalMs;
    m_canceled.store(false);
}

void TimerTask::run()
{
    if (m_canceled.load()) {
        return;
    }

    if (m_callback) {
        m_callback();
    }

    if (m_repeated) {
        if (!m_canceled.load()) {
            resetTime();
        }
        return;
    }

    cancel();
}

int64_t TimerTask::normalizeInterval(int64_t intervalMs) const
{
    if (intervalMs < 0) {
        return 0;
    }
    return intervalMs;
}

Timer::Timer(Reactor *reactor)
{
    // timerfd_create(2) 参数依次为：时钟类型、文件描述符标志。
    // CLOCK_MONOTONIC 使用单调时间，避免系统时间回拨影响定时；TFD_NONBLOCK
    // 让 read(2) 在没有到期事件时返回 EAGAIN；TFD_CLOEXEC 避免 fd 泄漏到子进程。
    m_timerFd = timerfd_create(CLOCK_MONOTONIC, TFD_NONBLOCK | TFD_CLOEXEC);
    if (m_timerFd < 0) {
        ErrorLog("timerfd_create failed, errno = " + std::to_string(errno));
        return;
    }

    m_fdEvent.setFd(m_timerFd);
    m_fdEvent.setReactor(reactor);
    m_fdEvent.addListenEvent(EPOLLIN);
    m_fdEvent.setReadCallback([this]() {
        handleTimerReadable();
    });

    if (!m_fdEvent.registerToReactor()) {
        ErrorLog("Timer registerToReactor failed, timerfd = " + std::to_string(m_timerFd));
    }
}

Timer::~Timer()
{
    m_fdEvent.unregisterFromReactor();
    if (m_timerFd >= 0) {
        close(m_timerFd);
        m_timerFd = -1;
    }
}

int Timer::getFd() const
{
    return m_timerFd;
}

bool Timer::addTimerTask(const std::shared_ptr<TimerTask>& task)
{
    if (task == nullptr || task->isCanceled()) {
        return false;
    }

    auto it = std::find(m_tasks.begin(), m_tasks.end(), task);
    if (it == m_tasks.end()) {
        m_tasks.push_back(task);
    }
    resetTimerFd();
    return true;
}

bool Timer::delTimerTask(const std::shared_ptr<TimerTask>& task)
{
    if (task == nullptr) {
        return false;
    }

    task->cancel();
    auto oldSize = m_tasks.size();
    removeCanceledTasks();
    resetTimerFd();
    return oldSize != m_tasks.size();
}

std::size_t Timer::getPendingTaskCount() const
{
    return m_tasks.size();
}

// 当 timerfd 可读（即定时器到期）时由事件循环回调本函数。
// 主要工作：1) 读出 timerfd 的过期次数；2) 找出所有已到期的任务并执行；
//          3) 把尚未到期 / 重复执行的任务放回任务集合；4) 重新设置下次到期时间。
void Timer::handleTimerReadable()
{
    // expirations 用于接收 timerfd 读取到的过期次数（uint64_t，8 字节）。
    uint64_t expirations = 0;
    while (true) {
        // read(2) 参数依次为：timerfd、接收过期次数的缓冲区地址、缓冲区长度。
        // timerfd 可读时读取 8 字节无符号整数，表示自上次读取后的过期次数。
        ssize_t n = read(m_timerFd, &expirations, sizeof(expirations));
        // 成功读取了完整的 8 字节，结束读取循环。
        if (n == static_cast<ssize_t>(sizeof(expirations))) {
            break;
        }
        // timerfd 默认非阻塞模式下没有数据可读时返回 EAGAIN/EWOULDBLOCK，直接退出。
        if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
            break;
        }
        // 被信号中断时需要重试读取。
        if (n < 0 && errno == EINTR) {
            continue;
        }
        // 其它读取错误：记录错误日志后退出循环。
        if (n < 0) {
            ErrorLog("timerfd read failed, errno = " + std::to_string(errno));
        }
        break;
    }

    // 获取当前时间戳（毫秒），用于判断任务是否已经到期。
    int64_t nowMs = getNowMs();
    // expiredTasks：收集本次需要触发的到期任务。
    std::vector<std::shared_ptr<TimerTask>> expiredTasks;
    // pendingTasks：收集尚未到期、仍需保留的任务，准备用来替换原任务集合。
    std::vector<std::shared_ptr<TimerTask>> pendingTasks;
    // 预分配容量，避免后续 push_back 时频繁扩容。
    pendingTasks.reserve(m_tasks.size());

    // 遍历当前所有任务，按是否到期进行分流。
    for (const auto& task : m_tasks) {
        // 跳过空指针或已被取消的任务。
        if (task == nullptr || task->isCanceled()) {
            continue;
        }
        // 若任务已到期，加入到期任务列表待执行。
        if (task->isExpired(nowMs)) {
            expiredTasks.push_back(task);
            continue;
        }
        // 未到期则保留，留待下次处理。
        pendingTasks.push_back(task);
    }

    // 按到期时间从早到晚排序，保证先到期的任务先被执行。
    std::sort(expiredTasks.begin(), expiredTasks.end(), [](const auto& left, const auto& right) {
        return left->getExpireTimeMs() < right->getExpireTimeMs();
    });

    // 用未到期任务列表替换原任务集合，相当于把已到期任务从集合中移除。
    m_tasks.swap(pendingTasks);
    // 依次执行到期任务；若任务未被取消（例如周期任务），则重新加回任务集合。
    for (const auto& task : expiredTasks) {
        task->run();
        if (!task->isCanceled()) {
            m_tasks.push_back(task);
        }
    }

    // 重新计算并设置 timerfd 的下次到期时间，使其对准最早到期的任务。
    resetTimerFd();
}

void Timer::resetTimerFd()
{
    if (m_timerFd < 0) {
        return;
    }

    removeCanceledTasks();

    struct itimerspec spec {};
    if (!m_tasks.empty()) {
        auto it = std::min_element(m_tasks.begin(), m_tasks.end(), [](const auto& left, const auto& right) {
            return left->getExpireTimeMs() < right->getExpireTimeMs();
        });

        int64_t nowMs = getNowMs();
        int64_t delayMs = (*it)->getExpireTimeMs() - nowMs;
        if (delayMs <= 0) {
            spec.it_value.tv_nsec = 1;
        } else {
            spec.it_value.tv_sec = delayMs / 1000;
            spec.it_value.tv_nsec = (delayMs % 1000) * 1000 * 1000;
        }
    }

    // timerfd_settime(2) 参数依次为：timerfd、flags、新超时时间、旧超时时间输出地址。
    // flags 为 0 表示使用相对时间；spec.it_value 为 0 时表示解除当前定时。
    if (timerfd_settime(m_timerFd, 0, &spec, nullptr) != 0) {
        ErrorLog("timerfd_settime failed, errno = " + std::to_string(errno));
    }
}

void Timer::removeCanceledTasks()
{
    m_tasks.erase(
        std::remove_if(
            m_tasks.begin(),
            m_tasks.end(),
            [](const auto& task) {
                return task == nullptr || task->isCanceled();
            }),
        m_tasks.end());
}

}
