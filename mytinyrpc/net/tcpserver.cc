#include "tcpserver.h"
#include "tcpconnection.h"
#include "comm/log.h"

#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#include <cstring>
#include <string>

namespace tinyrpc {

TcpServer::TcpServer(const IPAddress& addr)
  : m_addr(addr) {
  DebugLog("TcpServer constructed on " + m_addr.toString());
}

TcpServer::~TcpServer() {
  if (m_listen_fd >= 0) {
    close(m_listen_fd);
  }
}

const IPAddress& TcpServer::getLocalAddress() const {
  return m_addr;
}

bool TcpServer::init() {
  m_listen_fd = socket(AF_INET, SOCK_STREAM, 0);
  if (m_listen_fd < 0) {
    ErrorLog("create socket failed");
    return false;
  }

  int val = 1;
  // 设置 SO_REUSEADDR，允许端口在 TIME_WAIT 等状态下快速重用，避免程序重启后 bind 失败。
  // m_listen_fd 是监听 socket；SOL_SOCKET 表示操作套接字层选项；SO_REUSEADDR 是要设置的选项；
  // &val 传入选项值，这里 1 表示开启；sizeof(val) 是该值的字节大小。
  setsockopt(m_listen_fd, SOL_SOCKET, SO_REUSEADDR, &val, sizeof(val));

  int rt = bind(m_listen_fd, m_addr.getSockAddr(), m_addr.getSockLen());
  if (rt != 0) {
    ErrorLog("bind failed");
    return false;
  }

  // 第二个参数是监听队列的上限(backlog)，SOMAXCONN 表示交给系统使用默认的最大值
  rt = listen(m_listen_fd, SOMAXCONN);
  if (rt != 0) {
    ErrorLog("listen failed");
    return false;
  }

  InfoLog("TcpServer listen on " + m_addr.toString());
  return true;
}

void TcpServer::start() {
  InfoLog("TcpServer start accept loop on " + m_addr.toString());
  acceptLoop();
}

void TcpServer::acceptLoop() {
  while (true) {
    sockaddr_in client_addr {};
    socklen_t client_len = sizeof(client_addr);

    int client_fd = accept(m_listen_fd, reinterpret_cast<sockaddr*>(&client_addr), &client_len);

    if (client_fd < 0) {
      ErrorLog("accept failed");
      continue;
    }

    InfoLog("TcpServer accept client fd = " + std::to_string(client_fd));

    TcpConnection conn(client_fd);
    conn.handle();
  }
}

}
