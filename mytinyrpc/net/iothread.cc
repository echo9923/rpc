#include "net/iothread.h"

#include <chrono>
#include <utility>

namespace tinyrpc {

IOThread::IOThread()
{
    m_thread = std::thread([this]() {
        threadFunc();
    });

    while (!m_started.load()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
}

IOThread::~IOThread()
{
    stop();
}

Reactor* IOThread::getReactor()
{
    return &m_reactor;
}

const Reactor* IOThread::getReactor() const
{
    return &m_reactor;
}

std::thread::id IOThread::getThreadId() const
{
    return m_threadId;
}

bool IOThread::isStarted() const
{
    return m_started.load();
}

void IOThread::addTask(std::function<void()> task)
{
    m_reactor.addTask(std::move(task));
}

void IOThread::stop()
{
    if (!m_started.load() && !m_thread.joinable()) {
        return;
    }

    m_reactor.stop();
    if (m_thread.joinable()) {
        m_thread.join();
    }
}

void IOThread::threadFunc()
{
    m_threadId = std::this_thread::get_id();
    m_started.store(true);
    m_reactor.loop();
    m_started.store(false);
}

}
