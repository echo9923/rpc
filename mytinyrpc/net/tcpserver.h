#pragma once

#include "net/fdevent.h"
#include "net/netaddress.h"
#include "net/reactor.h"
#include "net/socket.h"

namespace tinyrpc {

class TcpServer {
 public:
    explicit TcpServer(const IPAddress& addr);
    ~TcpServer();

    const IPAddress& getLocalAddress() const;

    bool init();

    void start();

 private:
    void acceptLoop();

 private:
    IPAddress m_addr;
    Socket m_listenFd {kInvalidSocket};

    Reactor m_reactor;
    FdEvent m_listenEvent;
    bool m_running {false};
};

}
