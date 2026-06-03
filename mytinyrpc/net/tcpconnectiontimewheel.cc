#include "net/tcpconnectiontimewheel.h"

#include "comm/log.h"
#include "net/reactor.h"
#include "net/tcpconnection.h"

#include <algorithm>
#include <utility>

namespace tinyrpc {

TcpConnectionTimeWheel::TcpConnectionTimeWheel(
    Reactor *reactor,
    int64_t idleTimeoutMs,
    int64_t checkIntervalMs)
    : m_reactor(reactor),
      m_idleTimeoutMs(normalizeTimeout(idleTimeoutMs)),
      m_checkIntervalMs(normalizeCheckInterval(checkIntervalMs))
{
}

TcpConnectionTimeWheel::~TcpConnectionTimeWheel()
{
    if (m_reactor == nullptr || m_reactor->getTimer() == nullptr) {
        return;
    }

    for (auto& [fd, entry] : m_entries) {
        (void)fd;
        if (entry.m_timerEvent != nullptr) {
            m_reactor->getTimer()->delTimerEvent(entry.m_timerEvent);
        }
    }
}

bool TcpConnectionTimeWheel::addConnection(const std::shared_ptr<TcpConnection>& connection)
{
    if (m_reactor == nullptr || m_reactor->getTimer() == nullptr || connection == nullptr) {
        return false;
    }

    int fd = connection->getFd();
    if (fd < 0 || connection->isClosed()) {
        return false;
    }

    removeConnection(fd);
    connection->refreshActiveTime();

    auto event = std::make_shared<TimerEvent>(m_checkIntervalMs, true, [this, fd]() {
        onTimer(fd);
    });
    if (!m_reactor->getTimer()->addTimerEvent(event)) {
        return false;
    }

    m_entries[fd] = Entry {
        connection,
        std::move(event)
    };
    return true;
}

bool TcpConnectionTimeWheel::refreshConnection(int fd)
{
    auto iter = m_entries.find(fd);
    if (iter == m_entries.end()) {
        return false;
    }

    auto connection = iter->second.m_connection.lock();
    if (connection == nullptr || connection->isClosed()) {
        removeConnection(fd);
        return false;
    }

    connection->refreshActiveTime();
    if (iter->second.m_timerEvent != nullptr) {
        iter->second.m_timerEvent->resetTime(m_checkIntervalMs);
    }
    return true;
}

bool TcpConnectionTimeWheel::removeConnection(int fd)
{
    auto iter = m_entries.find(fd);
    if (iter == m_entries.end()) {
        return false;
    }

    if (m_reactor != nullptr && m_reactor->getTimer() != nullptr && iter->second.m_timerEvent != nullptr) {
        m_reactor->getTimer()->delTimerEvent(iter->second.m_timerEvent);
    }
    m_entries.erase(iter);
    return true;
}

bool TcpConnectionTimeWheel::hasConnection(int fd) const
{
    return m_entries.find(fd) != m_entries.end();
}

std::size_t TcpConnectionTimeWheel::getConnectionCount() const
{
    return m_entries.size();
}

int64_t TcpConnectionTimeWheel::normalizeTimeout(int64_t timeoutMs) const
{
    return std::max<int64_t>(timeoutMs, 1);
}

int64_t TcpConnectionTimeWheel::normalizeCheckInterval(int64_t checkIntervalMs) const
{
    return std::max<int64_t>(checkIntervalMs, 1);
}

void TcpConnectionTimeWheel::onTimer(int fd)
{
    auto iter = m_entries.find(fd);
    if (iter == m_entries.end()) {
        return;
    }

    auto connection = iter->second.m_connection.lock();
    if (connection == nullptr || connection->isClosed()) {
        removeConnection(fd);
        return;
    }

    int64_t idleMs = getNowMs() - connection->getLastActiveTimeMs();
    if (idleMs < m_idleTimeoutMs) {
        if (iter->second.m_timerEvent != nullptr) {
            int64_t nextDelayMs = std::max<int64_t>(m_checkIntervalMs, m_idleTimeoutMs - idleMs);
            iter->second.m_timerEvent->resetTime(nextDelayMs);
        }
        return;
    }

    closeConnectionInReactor(fd, connection);
}

void TcpConnectionTimeWheel::closeConnectionInReactor(
    int fd,
    const std::shared_ptr<TcpConnection>& connection)
{
    removeConnection(fd);
    InfoLog("TcpConnectionTimeWheel idle timeout, fd = " + std::to_string(fd));

    if (m_reactor == nullptr) {
        connection->closeWithCallback();
        return;
    }

    m_reactor->addTask([connection]() {
        connection->closeWithCallback();
    });
}

}
