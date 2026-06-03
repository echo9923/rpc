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
    const std::string& getLogPath() const;
    const std::string& getLogPrefix() const;
    int64_t getLogMaxSizeBytes() const;
    LogLevel getRpcLogLevel() const;
    LogLevel getAppLogLevel() const;
    int getLogSyncIntervalMs() const;
    int getCoroutineStackSizeBytes() const;
    int getCoroutinePoolSize() const;
    int getReqIdLen() const;
    int getMaxConnectTimeoutMs() const;
    int getTimeWheelBucketNum() const;
    int getTimeWheelIntervalSec() const;
    const std::string& getLastError() const;

 private:
    std::string m_serverHost {"127.0.0.1"};
    uint16_t m_serverPort {19999};
    std::string m_protocol {"tinypb"};
    int m_ioThreadNum {0};
    int m_timeoutMs {5000};
    std::string m_logPath {"logs"};
    std::string m_logPrefix {"mytinyrpc"};
    int64_t m_logMaxSizeBytes {64 * 1024 * 1024};
    LogLevel m_rpcLogLevel {LogLevel::Debug};
    LogLevel m_appLogLevel {LogLevel::Debug};
    int m_logSyncIntervalMs {1000};
    int m_coroutineStackSizeBytes {128 * 1024};
    int m_coroutinePoolSize {128};
    int m_reqIdLen {20};
    int m_maxConnectTimeoutMs {5000};
    int m_timeWheelBucketNum {60};
    int m_timeWheelIntervalSec {1};
    std::string m_lastError;
};

}
