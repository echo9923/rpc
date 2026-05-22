#include "tcpconnection.h"
#include "comm/log.h"

#include <unistd.h>

namespace tinyrpc {

TcpConnection::TcpConnection(int fd)
  : m_fd(fd) {
}

TcpConnection::~TcpConnection() {
  closeConnection();
}

int TcpConnection::getFd() const {
  return m_fd;
}

void TcpConnection::handle() {
  InfoLog("TcpConnection handle, fd = " + std::to_string(m_fd));
  closeConnection();
}

void TcpConnection::closeConnection() {
  if (m_fd >= 0) {
    InfoLog("TcpConnection close, fd = " + std::to_string(m_fd));
    close(m_fd);
    m_fd = -1;
  }
}

}
