#ifndef TINYRPC_NET_TCPSERVER_H
#define TINYRPC_NET_TCPSERVER_H

#include "net/netaddress.h"

namespace tinyrpc {

class TcpServer {
 public:
  explicit TcpServer(const IPAddress& addr);

  const IPAddress& getLocalAddress() const;

  bool init();

  void start();

 private:
  IPAddress m_addr;
};

}

#endif
