#include "netaddress.h"

namespace tinyrpc {

IPAddress::IPAddress(const std::string& ip, uint16_t port)
  : m_ip(ip), m_port(port) {
}

const std::string& IPAddress::getIp() const {
  return m_ip;
}

uint16_t IPAddress::getPort() const {
  return m_port;
}

std::string IPAddress::toString() const {
  return m_ip + ":" + std::to_string(m_port);
}

}
