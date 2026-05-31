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

TimerEvent::TimerEvent(int64_t intervalMs, bool repeated, Callback callback)
    : m_intervalMs(normalizeInterval(intervalMs)),
      m_repeated(repeated),
      m_callback(std::move(callback))
{
    resetTime(m_intervalMs);
}

int64_t TimerEvent::getIntervalMs() const
{
    return m_intervalMs;
}

int64_t TimerEvent::getExpireTimeMs() const
{
    return m_expireTimeMs;
}

bool TimerEvent::isRepeated() const
{
    return m_repeated;
}

bool TimerEvent::isCanceled() const
{
    return m_canceled;
}

bool TimerEvent::isExpired(int64_t nowMs) const
{
    return !m_canceled && nowMs >= m_expireTimeMs;
}

void TimerEvent::cancel()
{
    m_canceled = true;
}

void TimerEvent::resetTime()
{
    resetTime(m_intervalMs);
}

void TimerEvent::resetTime(int64_t intervalMs)
{
    m_intervalMs = normalizeInterval(intervalMs);
    m_expireTimeMs = getNowMs() + m_intervalMs;
    m_canceled = false;
}

void TimerEvent::run()
{
    if (m_canceled) {
        return;
    }

    if (m_callback) {
        m_callback();
    }

    if (m_repeated) {
        if (!m_canceled) {
            resetTime();
        }
        return;
    }

    cancel();
}

int64_t TimerEvent::normalizeInterval(int64_t intervalMs) const
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

bool Timer::addTimerEvent(const std::shared_ptr<TimerEvent>& event)
{
    if (event == nullptr || event->isCanceled()) {
        return false;
    }

    auto it = std::find(m_events.begin(), m_events.end(), event);
    if (it == m_events.end()) {
        m_events.push_back(event);
    }
    resetTimerFd();
    return true;
}

bool Timer::delTimerEvent(const std::shared_ptr<TimerEvent>& event)
{
    if (event == nullptr) {
        return false;
    }

    event->cancel();
    auto oldSize = m_events.size();
    removeCanceledEvents();
    resetTimerFd();
    return oldSize != m_events.size();
}

std::size_t Timer::getPendingEventCount() const
{
    return m_events.size();
}

void Timer::handleTimerReadable()
{
    uint64_t expirations = 0;
    while (true) {
        // read(2) 参数依次为：timerfd、接收过期次数的缓冲区地址、缓冲区长度。
        // timerfd 可读时读取 8 字节无符号整数，表示自上次读取后的过期次数。
        ssize_t n = read(m_timerFd, &expirations, sizeof(expirations));
        if (n == static_cast<ssize_t>(sizeof(expirations))) {
            break;
        }
        if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
            break;
        }
        if (n < 0 && errno == EINTR) {
            continue;
        }
        if (n < 0) {
            ErrorLog("timerfd read failed, errno = " + std::to_string(errno));
        }
        break;
    }

    int64_t nowMs = getNowMs();
    std::vector<std::shared_ptr<TimerEvent>> expiredEvents;
    std::vector<std::shared_ptr<TimerEvent>> pendingEvents;
    pendingEvents.reserve(m_events.size());

    for (const auto& event : m_events) {
        if (event == nullptr || event->isCanceled()) {
            continue;
        }
        if (event->isExpired(nowMs)) {
            expiredEvents.push_back(event);
            continue;
        }
        pendingEvents.push_back(event);
    }

    std::sort(expiredEvents.begin(), expiredEvents.end(), [](const auto& left, const auto& right) {
        return left->getExpireTimeMs() < right->getExpireTimeMs();
    });

    m_events.swap(pendingEvents);
    for (const auto& event : expiredEvents) {
        event->run();
        if (!event->isCanceled()) {
            m_events.push_back(event);
        }
    }

    resetTimerFd();
}

void Timer::resetTimerFd()
{
    if (m_timerFd < 0) {
        return;
    }

    removeCanceledEvents();

    struct itimerspec spec {};
    if (!m_events.empty()) {
        auto it = std::min_element(m_events.begin(), m_events.end(), [](const auto& left, const auto& right) {
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

void Timer::removeCanceledEvents()
{
    m_events.erase(
        std::remove_if(
            m_events.begin(),
            m_events.end(),
            [](const auto& event) {
                return event == nullptr || event->isCanceled();
            }),
        m_events.end());
}

}
