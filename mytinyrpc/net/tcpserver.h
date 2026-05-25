#pragma once

#include "net/netaddress.h"
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
};

}
