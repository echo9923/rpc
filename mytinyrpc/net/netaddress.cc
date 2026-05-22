#include "netaddress.h"

#include <arpa/inet.h>

namespace tinyrpc {

IPAddress::IPAddress(const std::string& ip, uint16_t port)
  : m_ip(ip), m_port(port) {
  m_addr.sin_family = AF_INET;
  m_addr.sin_port = htons(m_port);
  inet_pton(AF_INET, m_ip.c_str(), &m_addr.sin_addr);
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

const sockaddr* IPAddress::getSockAddr() const {
  return reinterpret_cast<const sockaddr*>(&m_addr);
}

socklen_t IPAddress::getSockLen() const {
  return sizeof(m_addr);
}

}
