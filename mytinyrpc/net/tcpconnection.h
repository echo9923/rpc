#pragma once

#include "net/socket.h"

#include <string>

namespace tinyrpc {

class TcpConnection {
 public:
  explicit TcpConnection(Socket fd);

  ~TcpConnection();

  Socket getFd() const;

  void handle();

  void closeConnection();

 private:
  std::string readData();

  bool writeData(const std::string& data);

 private:
  Socket m_fd {kInvalidSocket};
};

}
