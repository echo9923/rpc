#include "net/fdevent.h"

#include <sys/epoll.h>

namespace tinyrpc {

FdEvent::FdEvent(int fd)
    : m_fd(fd)
{
}

void FdEvent::setFd(int fd)
{
    m_fd = fd;
}

int FdEvent::getFd() const
{
    return m_fd;
}

void FdEvent::addListenEvent(uint32_t event)
{
    m_listenEvents |= event;
}

void FdEvent::delListenEvent(uint32_t event)
{
    m_listenEvents &= ~event;
}

uint32_t FdEvent::getListenEvents() const
{
    return m_listenEvents;
}

void FdEvent::setReadCallback(std::function<void()> cb)
{
    m_readCallback = std::move(cb);
}

void FdEvent::setWriteCallback(std::function<void()> cb)
{
    m_writeCallback = std::move(cb);
}

void FdEvent::handleEvent(uint32_t triggerEvents)
{
    if ((triggerEvents & EPOLLIN) && m_readCallback) {
        m_readCallback();
    }
    if ((triggerEvents & EPOLLOUT) && m_writeCallback) {
        m_writeCallback();
    }
}

}
