#ifndef TINYRPC_NET_TCPSERVER_H
#define TINYRPC_NET_TCPSERVER_H

#include "net/netaddress.h"

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
  int m_listen_fd {-1};
};

}

#endif
