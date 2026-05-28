#include "net/reactor.h"

#include "comm/log.h"
#include "coroutine/coroutine.h"

#include <cerrno>
#include <cstring>
#include <string>
#include <sys/epoll.h>
#include <unistd.h>

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
    }
}

Reactor::~Reactor()
{
    if (m_epollFd >= 0) {
        close(m_epollFd);
        m_epollFd = -1;
    }
}

int Reactor::getEpollFd() const
{
    return m_epollFd;
}

bool Reactor::epollAdd(FdEvent* event)
{
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
        ErrorLog(
            "epoll_ctl ADD failed, fd = " + std::to_string(event->getFd()) +
            ", errno = " + std::to_string(errno)
        );
        return false;
    }

    m_events[event->getFd()] = event;
    return true;
}

bool Reactor::epollMod(FdEvent* event)
{
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

        // 如果 FdEvent 上挂有协程，说明此前 read_hook/write_hook 遇到 EAGAIN
        // 后将协程挂起并注册了 epoll 事件。此时 fd 已就绪，恢复协程继续执行。
        // 先 clearCoroutine() 再 resume()，避免协程恢复结束后 FdEvent 还留着旧指针。
        //
        // 通过 getCoroutineListenEvent() 获取协程正在等待的事件类型（EPOLLIN 或 EPOLLOUT），
        // 只有当触发事件与等待事件匹配时才恢复协程，避免在错误事件上恢复。
        // 例如：协程等 EPOLLOUT 时，EPOLLIN 触发不应恢复协程。
        Coroutine* coroutine = event->getCoroutine();
        uint32_t waitEvent = event->getCoroutineListenEvent();

        if (coroutine != nullptr && waitEvent != 0 && (events[i].events & waitEvent)) {
            event->clearCoroutine();
            coroutine->resume();
            continue;
        }

        event->handleEvent(events[i].events);
    }

    return nfds;
}

}
