#ifndef TINYRPC_NET_NETADDRESS_H
#define TINYRPC_NET_NETADDRESS_H

#include <string>
#include <cstdint>

namespace tinyrpc {

class IPAddress {
 public:
  IPAddress(const std::string& ip, uint16_t port);

  const std::string& getIp() const;
  uint16_t getPort() const;
  std::string toString() const;

 private:
  std::string m_ip;
  uint16_t m_port;
};

}

#endif
