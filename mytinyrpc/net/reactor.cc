#include "net/reactor.h"

#include "comm/log.h"
#include "coroutine/coroutine.h"
#include "net/timer.h"

#include <cerrno>
#include <cstring>
#include <string>
#include <sys/eventfd.h>
#include <sys/epoll.h>
#include <unistd.h>
#include <utility>

namespace tinyrpc {

Reactor::Reactor()
{
    // epoll_create1(EPOLL_CLOEXEC) 创建一个 epoll 实例，返回 epoll fd。
    // EPOLL_CLOEXEC 保证在执行 exec 时自动关闭该 fd，防止泄漏到子进程。
    m_epollFd = epoll_create1(EPOLL_CLOEXEC);
    if (m_epollFd < 0) {
        ErrorLog(
            "epoll_create1 failed, errno = " + std::to_string(errno)
        );
        return;
    }

    m_timer = std::make_unique<Timer>(this);
    initWakeupFd();
}

Reactor::~Reactor()
{
    m_wakeupEvent.unregisterFromReactor();
    if (m_wakeupFd >= 0) {
        close(m_wakeupFd);
        m_wakeupFd = -1;
    }
    m_timer.reset();
    if (m_epollFd >= 0) {
        close(m_epollFd);
        m_epollFd = -1;
    }
}

static thread_local Reactor* s_currentReactor = nullptr;

void Reactor::setCurrentReactor(Reactor* reactor) { s_currentReactor = reactor; }
Reactor* Reactor::getCurrentReactor()             { return s_currentReactor; }

int Reactor::getEpollFd() const
{
    return m_epollFd;
}

Timer* Reactor::getTimer() const
{
    return m_timer.get();
}

void Reactor::addTask(std::function<void()> task)
{
    if (!task) {
        return;
    }

    {
        std::lock_guard<std::mutex> lock(m_taskMutex);
        m_pendingTasks.push(std::move(task));
    }

    wakeup();
}

void Reactor::loop()
{
    m_stop.store(false);
    while (!m_stop.load()) {
        waitOnce(-1);
    }

    // stop() 可能与 addTask() 紧邻发生，退出前再执行一次队列中的任务，
    // 保证已经投递的连接关闭任务不会因为线程停止而遗留。
    runPendingTasks();
}

void Reactor::stop()
{
    m_stop.store(true);
    wakeup();
}

bool Reactor::addFdEvent(FdEvent* event)
{
    if (event == nullptr) {
        return false;
    }

    event->setReactor(this);
    return event->registerToReactor();
}

bool Reactor::delFdEvent(FdEvent* event)
{
    if (event == nullptr) {
        return false;
    }

    return event->unregisterFromReactor();
}

bool Reactor::epollAdd(FdEvent* event)
{
    if (event == nullptr || event->getFd() < 0 || m_epollFd < 0) {
        return false;
    }

    auto iter = m_events.find(event->getFd());
    if (iter != m_events.end()) {
        ErrorLog(
            "epoll_ctl ADD rejected duplicate fd = " +
            std::to_string(event->getFd())
        );
        return iter->second == event;
    }

    struct epoll_event ev;
    ev.events = event->getListenEvents();
    // data 是 epoll_event 的联合体，有 ptr / fd / u32 / u64 四种存法。
    // 使用 data.ptr 直接指向 FdEvent 对象，epoll_wait 返回后可以
    // 直接拿到对象指针调用 handleEvent()，省去一次 fd→FdEvent 的查表。
    ev.data.ptr = event;

    // epoll_ctl(EPOLL_CTL_ADD) 将 fd 及其关注事件注册到 epoll 实例。
    // 此调用是同步的：内核在返回前已拷贝完 events / data，局部变量 ev 的地址
    // 不会被内核记住，函数返回后安全销毁。
    if (epoll_ctl(m_epollFd, EPOLL_CTL_ADD, event->getFd(), &ev) < 0) {
      ErrorLog("epoll_ctl ADD failed, fd = " + std::to_string(event->getFd()) +
               ", errno = " + std::to_string(errno));
      return false;
    }

    m_events[event->getFd()] = event;
    return true;
}

bool Reactor::epollMod(FdEvent* event)
{
    if (event == nullptr || event->getFd() < 0 || m_epollFd < 0) {
        return false;
    }

    auto iter = m_events.find(event->getFd());
    if (iter == m_events.end() || iter->second != event) {
        ErrorLog(
            "epoll_ctl MOD rejected unregistered fd = " +
            std::to_string(event->getFd())
        );
        return false;
    }

    // epoll_ctl(EPOLL_CTL_MOD) 修改已注册 fd 的关注事件。
    // 用于连接对象按需启停 EPOLLIN / EPOLLOUT。
    struct epoll_event ev;
    ev.events = event->getListenEvents();
    ev.data.ptr = event;

    if (epoll_ctl(m_epollFd, EPOLL_CTL_MOD, event->getFd(), &ev) < 0) {
        ErrorLog(
            "epoll_ctl MOD failed, fd = " + std::to_string(event->getFd()) +
            ", errno = " + std::to_string(errno)
        );
        return false;
    }

    m_events[event->getFd()] = event;
    return true;
}

bool Reactor::epollDel(FdEvent* event)
{
    if (event == nullptr || event->getFd() < 0 || m_epollFd < 0) {
        return false;
    }

    auto iter = m_events.find(event->getFd());
    if (iter == m_events.end()) {
        return true;
    }
    if (iter->second != event) {
        ErrorLog(
            "epoll_ctl DEL rejected non-owner fd = " +
            std::to_string(event->getFd())
        );
        return false;
    }

    // epoll_ctl(EPOLL_CTL_DEL) 从 epoll 实例中删除指定 fd。
    // 第四个参数可传 nullptr，因为 EPOLL_CTL_DEL 会忽略 events 数据。
    if (epoll_ctl(m_epollFd, EPOLL_CTL_DEL, event->getFd(), nullptr) < 0) {
        ErrorLog(
            "epoll_ctl DEL failed, fd = " + std::to_string(event->getFd()) +
            ", errno = " + std::to_string(errno)
        );
        return false;
    }

    m_events.erase(event->getFd());
    return true;
}

int Reactor::waitOnce(int timeoutMs)
{
    // epoll_wait 等待注册的 fd 上发生事件。
    // timeoutMs：超时时间（毫秒），-1 表示无限等待，0 表示立即返回。
    struct epoll_event events[kMaxEvents];
    int nfds = epoll_wait(m_epollFd, events, kMaxEvents, timeoutMs);
    if (nfds < 0) {
        if (errno == EINTR) {
            // 被信号中断，不属于错误，返回 0。
            return 0;
        }
        ErrorLog(
            "epoll_wait failed, errno = " + std::to_string(errno)
        );
        return -1;
    }

    for (int i = 0; i < nfds; ++i) {
        // data.ptr 在 epollAdd 时已被设为 FdEvent*，直接还原调用。
        auto* event = static_cast<FdEvent*>(events[i].data.ptr);

        // 如果 FdEvent 上挂有协程，说明此前 readHook/writeHook 遇到 EAGAIN
        // 后将协程挂起并注册了 epoll 事件。此时 fd 已就绪，恢复协程继续执行。
        // 先 clearCoroutine() 再 resume()，避免协程恢复结束后 FdEvent 还留着旧指针。
        //
        // 通过 getCoroutineListenEvent() 获取协程正在等待的事件类型（EPOLLIN 或 EPOLLOUT），
        // 只有当触发事件与等待事件匹配时才恢复协程，避免在错误事件上恢复。
        // 例如：协程等 EPOLLOUT 时，EPOLLIN 触发不应恢复协程。
        Coroutine* coroutine = event->getCoroutine();
        uint32_t waitEvent = event->getCoroutineListenEvent();

        // epoll_wait 拿到一批就绪 fd，每个 fd 只可能来自两种情况：
        //   A) 协程异步等待：之前 readHook/writeHook 遇到 EAGAIN，把协程挂到本
        //      FdEvent 上、登记了它在等的事件（EPOLLIN/EPOLLOUT）后让出执行权。
        //      此时 fd 就绪，需要唤醒协程从挂起点继续读/写。
        //   B) 普通回调：fd 上没有挂协程，走下面的 handleEvent。
        //
        // 三个条件全部成立，才认定是 A 路并恢复协程：
        //   - coroutine 非空、waitEvent 非空：二者是一次“挂起等待”同时留下的两
        //     个标记（挂了协程，也登记了等什么事件），合起来表示“确有协程在等”。
        //   - (events[i].events & waitEvent)：本次 epoll 实际触发的事件，与协程
        //     在等的事件有交集。一个 fd 可能同时监听 IN 和 OUT，但协程只等其中
        //     一个——比如协程在等可写，这次只就绪了“可读”，就不该唤醒它。
        if (coroutine != nullptr && waitEvent != 0 && (events[i].events & waitEvent)) {
            // 先清除 FdEvent 上的协程指针，再 resume，防止协程恢复后 FdEvent 还残留旧指针。
            event->clearCoroutine();
            // 唤醒挂起的协程，让它从 readHook/writeHook 的挂起点继续向下执行。
            coroutine->resume();
            // 当前事件已由协程处理完，跳过下面的普通 handleEvent，直接处理下一个就绪事件。
            continue;
        }

        event->handleEvent(events[i].events);
    }

    return nfds;
}

bool Reactor::initWakeupFd()
{
    // 创建用于唤醒 Reactor 的 eventfd。
    // eventfd(2) 参数依次为：初始计数值、文件描述符标志。
    // EFD_NONBLOCK 让 read(2) 在计数为 0 时返回 EAGAIN；EFD_CLOEXEC
    // 避免 fd 泄漏到 exec 后的子进程。
    m_wakeupFd = eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC);
    if (m_wakeupFd < 0) {
        // eventfd 创建失败，记录错误并返回 false，由上层决定如何处理。
        ErrorLog("eventfd failed, errno = " + std::to_string(errno));
        return false;
    }

    // 将 wakeup eventfd 封装到 FdEvent 对象中，便于复用 Reactor 的事件管理机制。
    m_wakeupEvent.setFd(m_wakeupFd);
    // 指向所属 Reactor，供 FdEvent 在回调中访问 reactor 相关信息。
    m_wakeupEvent.setReactor(this);
    // 监听可读事件：向 eventfd 写入数据后，epoll 会触发 EPOLLIN。
    m_wakeupEvent.addListenEvent(EPOLLIN);
    // 设置读回调：当 eventfd 可读时调用 handleWakeup()，消费计数并清空唤醒信号。
    m_wakeupEvent.setReadCallback([this]() {
        handleWakeup();
    });

    // 将 wakeup 事件注册到 epoll 实例，开始监听其就绪状态。
    // 注册失败说明 epoll_ctl 出错，无法正常工作，需返回 false。
    if (!m_wakeupEvent.registerToReactor()) {
        ErrorLog("wakeup event registerToReactor failed, fd = " + std::to_string(m_wakeupFd));
        return false;
    }

    return true;
}

void Reactor::wakeup()
{
    if (m_wakeupFd < 0) {
        return;
    }

    uint64_t value = 1;
    while (true) {
        // write(2) 参数依次为：eventfd、待写计数值地址、写入长度。
        // 向 eventfd 写入 8 字节整数会累加计数，使 epoll_wait() 被 EPOLLIN 唤醒。
        ssize_t n = write(m_wakeupFd, &value, sizeof(value));
        if (n == static_cast<ssize_t>(sizeof(value))) {
            return;
        }
        if (n < 0 && errno == EINTR) {
            continue;
        }
        if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
            return;
        }
        if (n < 0) {
            ErrorLog("eventfd write failed, errno = " + std::to_string(errno));
        }
        return;
    }
}

void Reactor::handleWakeup()
{
    uint64_t value = 0;
    while (true) {
        // read(2) 参数依次为：eventfd、接收计数值地址、读取长度。
        // 读取 eventfd 会取出当前计数并清零，随后执行已投递任务。
        ssize_t n = read(m_wakeupFd, &value, sizeof(value));
        if (n == static_cast<ssize_t>(sizeof(value))) {
            continue;
        }
        if (n < 0 && errno == EINTR) {
            continue;
        }
        if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
            break;
        }
        if (n < 0) {
            ErrorLog("eventfd read failed, errno = " + std::to_string(errno));
        }
        break;
    }

    runPendingTasks();
}

void Reactor::runPendingTasks()
{
    std::queue<std::function<void()>> tasks;
    {
        std::lock_guard<std::mutex> lock(m_taskMutex);
        tasks.swap(m_pendingTasks);
    }

    while (!tasks.empty()) {
        auto task = std::move(tasks.front());
        tasks.pop();
        if (task) {
            task();
        }
    }
}

}
