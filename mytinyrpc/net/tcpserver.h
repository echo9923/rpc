#pragma once

#include "net/abstractcodec.h"
#include "net/fdevent.h"
#include "net/netaddress.h"
#include "net/reactor.h"
#include "net/socket.h"

#include <memory>
#include <unordered_map>

namespace tinyrpc {

class TcpConnection;

class TcpServer {
 public:
    explicit TcpServer(const IPAddress& addr, AbstractCodec::Ptr codec = nullptr);
    ~TcpServer();

    const IPAddress& getLocalAddress() const;

    bool init();

    void start();

 private:
    void acceptLoop();

    void removeConnection(int fd);

 private:
    IPAddress m_addr;
    Socket m_listenFd {kInvalidSocket};

    Reactor m_reactor;
    FdEvent m_listenEvent;
    AbstractCodec::Ptr m_codec;
    std::unordered_map<int, std::shared_ptr<TcpConnection>> m_connections;
    bool m_running {false};
};

}
