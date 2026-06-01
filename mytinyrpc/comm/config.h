#pragma once

#include "comm/log.h"

#include <cstdint>
#include <string>

namespace tinyrpc {

// Config 保存框架启动所需的最小配置。
// XML 中缺失字段会继续使用这里的默认值。
class Config {
 public:
    bool loadFromXml(const std::string& path);
    const std::string& getServerHost() const;
    uint16_t getServerPort() const;
    const std::string& getProtocol() const;
    int getIOThreadNum() const;
    int getTimeoutMs() const;
    LogLevel getLogLevel() const;
    const std::string& getLastError() const;

 private:
    std::string m_serverHost {"127.0.0.1"};
    uint16_t m_serverPort {19999};
    std::string m_protocol {"tinypb"};
    int m_ioThreadNum {0};
    int m_timeoutMs {5000};
    LogLevel m_logLevel {LogLevel::Debug};
    std::string m_lastError;
};

}
