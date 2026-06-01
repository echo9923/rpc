#pragma once

#include "comm/log.h"

#include <cstdint>
#include <string>

namespace tinyrpc {

// Config 保存框架启动所需的最小配置。
// 当前阶段只提供默认值，不读取 XML。
class Config {
 public:
    const std::string& getServerHost() const;
    uint16_t getServerPort() const;
    const std::string& getProtocol() const;
    int getIOThreadNum() const;
    int getTimeoutMs() const;
    LogLevel getLogLevel() const;

 private:
    std::string m_serverHost {"127.0.0.1"};
    uint16_t m_serverPort {19999};
    std::string m_protocol {"tinypb"};
    int m_ioThreadNum {0};
    int m_timeoutMs {5000};
    LogLevel m_logLevel {LogLevel::Debug};
};

}
