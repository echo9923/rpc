#ifndef TINYRPC_NET_TCPCONNECTION_H
#define TINYRPC_NET_TCPCONNECTION_H

#include <string>

namespace tinyrpc {

class TcpConnection {
 public:
  explicit TcpConnection(int fd);

  ~TcpConnection();

  int getFd() const;

  void handle();

  void closeConnection();

 private:
  std::string readData();

  bool writeData(const std::string& data);

 private:
  int m_fd {-1};
};

}

#endif
