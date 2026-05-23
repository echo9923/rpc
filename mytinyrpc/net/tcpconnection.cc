#include "net/tcpconnection.h"
#include "comm/log.h"

#include <unistd.h>

namespace tinyrpc {

TcpConnection::TcpConnection(Socket fd)
  : m_fd(fd)
{
}

TcpConnection::~TcpConnection()
{
  closeConnection();
}

Socket TcpConnection::getFd() const
{
  return m_fd;
}

void TcpConnection::handle()
{
  InfoLog("TcpConnection handle, fd = " + std::to_string(m_fd));

  while (true) {
    std::string data = readData();

    if (data.empty()) {
      InfoLog("TcpConnection no more data, fd = " + std::to_string(m_fd));
      break;
    }

    InfoLog("TcpConnection receive from fd = " + std::to_string(m_fd) + ", data = " + data);

    if (!writeData(data)) {
      ErrorLog("TcpConnection write failed, fd = " + std::to_string(m_fd));
      break;
    }
  }

  closeConnection();
}

void TcpConnection::closeConnection()
{
  if (m_fd >= 0) {
    InfoLog("TcpConnection close, fd = " + std::to_string(m_fd));
    close(m_fd);
    m_fd = -1;
  }
}

std::string TcpConnection::readData()
{
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

bool TcpConnection::writeData(const std::string& data)
{
  size_t total_written = 0;

  while (total_written < data.size()) {
    ssize_t n = write(
      m_fd,
      data.data() + total_written,
      data.size() - total_written
    );

    if (n < 0) {
      ErrorLog("write failed, fd = " + std::to_string(m_fd));
      return false;
    }

    if (n == 0) {
      ErrorLog("write returned 0, fd = " + std::to_string(m_fd));
      return false;
    }

    total_written += static_cast<size_t>(n);
  }

  InfoLog(
    "TcpConnection write to fd = " +
    std::to_string(m_fd) +
    ", bytes = " +
    std::to_string(total_written)
  );

  return true;
}

}
