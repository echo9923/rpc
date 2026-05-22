#include "tcpserver.h"
#include "comm/log.h"

namespace tinyrpc {

TcpServer::TcpServer(const IPAddress& addr)
  : m_addr(addr) {
  DebugLog("TcpServer constructed on " + m_addr.toString());
}

const IPAddress& TcpServer::getLocalAddress() const {
  return m_addr;
}

bool TcpServer::init() {
  DebugLog("TcpServer init on " + m_addr.toString());
  return true;
}

void TcpServer::start() {
  DebugLog("TcpServer start on " + m_addr.toString());
}

}
