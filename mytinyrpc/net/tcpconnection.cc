#include "tcpconnection.h"
#include "comm/log.h"

namespace tinyrpc {

TcpConnection::TcpConnection(int fd)
  : m_fd(fd) {
}

int TcpConnection::getFd() const {
  return m_fd;
}

void TcpConnection::handle() {
  DebugLog("TcpConnection::handle called, fd = " + std::to_string(m_fd));
}

}
