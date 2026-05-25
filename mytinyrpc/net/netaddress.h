#pragma once

#include <cstdint>
#include <netinet/in.h>
#include <string>

namespace tinyrpc {

class IPAddress {
 public:
    IPAddress(const std::string& ip, uint16_t port);

    const std::string& getIp() const;
    uint16_t getPort() const;
    std::string toString() const;

    const sockaddr* getSockAddr() const;
    socklen_t getSockLen() const;

 private:
    std::string m_ip;
    uint16_t m_port;
    sockaddr_in m_addr {};
};

}
