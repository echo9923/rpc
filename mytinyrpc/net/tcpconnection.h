#ifndef TINYRPC_NET_TCPCONNECTION_H
#define TINYRPC_NET_TCPCONNECTION_H

namespace tinyrpc {

class TcpConnection {
 public:
  explicit TcpConnection(int fd);

  int getFd() const;

  void handle();

 private:
  int m_fd {-1};
};

}

#endif
