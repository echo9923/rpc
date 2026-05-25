#include "net/reactor.h"

#include "comm/log.h"

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

bool Reactor::addEvent(FdEvent* event)
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

bool Reactor::modEvent(FdEvent* event)
{
    // epoll_ctl(EPOLL_CTL_MOD) 修改已注册 fd 的关注事件。
    // 用于 enableWriteEvent / disableWriteEvent 添加或删除 EPOLLOUT。
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

bool Reactor::delEvent(FdEvent* event)
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
        // data.ptr 在 addEvent 时已被设为 FdEvent*，直接还原调用。
        auto* event = static_cast<FdEvent*>(events[i].data.ptr);
        event->handleEvent(events[i].events);
    }

    return nfds;
}

}
