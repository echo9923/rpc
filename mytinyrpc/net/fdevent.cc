#include "net/fdevent.h"

#include "coroutine/coroutine.h"
#include "net/reactor.h"

#include <sys/epoll.h>

#include <utility>

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

void FdEvent::setReactor(Reactor *reactor)
{
    m_reactor = reactor;
}

Reactor *FdEvent::getReactor() const
{
    return m_reactor;
}

void FdEvent::setCoroutine(Coroutine *coroutine)
{
    m_coroutine = coroutine;
}

Coroutine *FdEvent::getCoroutine() const
{
    return m_coroutine;
}

void FdEvent::setCoroutineListenEvent(uint32_t event)
{
    m_coroutineListenEvent = event;
}

uint32_t FdEvent::getCoroutineListenEvent() const
{
    return m_coroutineListenEvent;
}

void FdEvent::clearCoroutine()
{
    m_coroutine = nullptr;
    m_coroutineListenEvent = 0;
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

bool FdEvent::registerToReactor()
{
    if (m_isRegistered) {
        return true;
    }

    if (m_reactor == nullptr || m_fd < 0) {
        return false;
    }

    m_isRegistered = m_reactor->epollAdd(this);
    return m_isRegistered;
}

bool FdEvent::updateToReactor()
{
    if (!m_isRegistered || m_reactor == nullptr || m_fd < 0) {
        return false;
    }

    return m_reactor->epollMod(this);
}

bool FdEvent::unregisterFromReactor()
{
    if (!m_isRegistered) {
        return true;
    }

    if (m_reactor == nullptr || m_fd < 0) {
        return false;
    }

    if (!m_reactor->epollDel(this)) {
        return false;
    }

    m_isRegistered = false;
    return true;
}

bool FdEvent::isRegistered() const
{
    return m_isRegistered;
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
    auto readCallback = m_readCallback;
    auto writeCallback = m_writeCallback;

    if ((triggerEvents & EPOLLIN) && readCallback) {
        readCallback();
    }
    if ((triggerEvents & EPOLLOUT) && writeCallback) {
        writeCallback();
    }
}

}
