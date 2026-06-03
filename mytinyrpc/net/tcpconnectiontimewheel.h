#pragma once

#include "net/timer.h"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <unordered_map>

namespace tinyrpc {

class Reactor;
class TcpConnection;

// TcpConnectionTimeWheel 是阶段 10 的简化空闲超时管理器。
// 当前不实现复杂分层时间轮，而是为每条连接挂一个 TimerTask：
// 定时器到期时检查连接最后活跃时间，真正超时才把 close 投递回连接所属 Reactor。
class TcpConnectionTimeWheel {
 public:
    TcpConnectionTimeWheel(Reactor *reactor, int64_t idleTimeoutMs, int64_t checkIntervalMs);
    ~TcpConnectionTimeWheel();

    bool addConnection(const std::shared_ptr<TcpConnection>& connection);
    bool refreshConnection(int fd);
    bool removeConnection(int fd);
    bool hasConnection(int fd) const;
    std::size_t getConnectionCount() const;

 private:
    struct Entry {
        std::weak_ptr<TcpConnection> m_connection;
        std::shared_ptr<TimerTask> m_timerTask;
    };

    int64_t normalizeTimeout(int64_t timeoutMs) const;
    int64_t normalizeCheckInterval(int64_t checkIntervalMs) const;
    void onTimer(int fd);
    void closeConnectionInReactor(int fd, const std::shared_ptr<TcpConnection>& connection);

    Reactor *m_reactor {nullptr};
    int64_t m_idleTimeoutMs {0};
    int64_t m_checkIntervalMs {0};
    std::unordered_map<int, Entry> m_entries;
};

}
