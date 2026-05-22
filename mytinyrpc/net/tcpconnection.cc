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

  std::string data = readData();

  if (!data.empty()) {
    InfoLog("TcpConnection receive from fd = " + std::to_string(m_fd) + ", data = " + data);
  } else {
    InfoLog("TcpConnection receive empty data, fd = " + std::to_string(m_fd));
  }

  closeConnection();
}

void TcpConnection::closeConnection() {
  if (m_fd >= 0) {
    InfoLog("TcpConnection close, fd = " + std::to_string(m_fd));
    close(m_fd);
    m_fd = -1;
  }
}

std::string TcpConnection::readData() {
  char buffer[1024] = {0};

  ssize_t n = read(m_fd, buffer, sizeof(buffer) - 1);

  if (n < 0) {
    ErrorLog("read failed, fd = " + std::to_string(m_fd));
    return "";
  }

  if (n == 0) {
    InfoLog("client closed before send data, fd = " + std::to_string(m_fd));
    return "";
  }

  return std::string(buffer, static_cast<size_t>(n));
}

}
