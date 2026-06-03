#include "comm/config.h"

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <fstream>
#include <optional>
#include <sstream>

namespace tinyrpc {

namespace {

std::string trim(const std::string& value)
{
    auto begin = std::find_if_not(value.begin(), value.end(), [](unsigned char ch) {
        return std::isspace(ch);
    });
    auto end = std::find_if_not(value.rbegin(), value.rend(), [](unsigned char ch) {
        return std::isspace(ch);
    }).base();
    if (begin >= end) {
        return "";
    }
    return std::string(begin, end);
}

std::string toLower(std::string value)
{
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });
    return value;
}

std::optional<std::string> findTagValue(const std::string& xml, const std::string& tagName)
{
    std::string beginTag = "<" + tagName + ">";
    std::string endTag = "</" + tagName + ">";
    size_t beginPos = xml.find(beginTag);
    if (beginPos == std::string::npos) {
        return std::nullopt;
    }

    beginPos += beginTag.size();
    size_t endPos = xml.find(endTag, beginPos);
    if (endPos == std::string::npos) {
        return std::nullopt;
    }
    return trim(xml.substr(beginPos, endPos - beginPos));
}

bool parseIntValue(const std::string& value, int minValue, int maxValue, int& result, std::string& error)
{
    try {
        size_t used = 0;
        int parsed = std::stoi(value, &used);
        if (used != value.size()) {
            error = "invalid integer value: " + value;
            return false;
        }
        if (parsed < minValue || parsed > maxValue) {
            error = "integer value out of range: " + value;
            return false;
        }
        result = parsed;
        return true;
    } catch (const std::exception&) {
        error = "invalid integer value: " + value;
        return false;
    }
}

bool parseInt64Value(
    const std::string& value,
    int64_t minValue,
    int64_t maxValue,
    int64_t& result,
    std::string& error
)
{
    try {
        size_t used = 0;
        int64_t parsed = std::stoll(value, &used);
        if (used != value.size()) {
            error = "invalid integer value: " + value;
            return false;
        }
        if (parsed < minValue || parsed > maxValue) {
            error = "integer value out of range: " + value;
            return false;
        }
        result = parsed;
        return true;
    } catch (const std::exception&) {
        error = "invalid integer value: " + value;
        return false;
    }
}

bool parseServerAddr(
    const std::string& value,
    std::string& serverHost,
    uint16_t& serverPort,
    std::string& error
)
{
    size_t colonPos = value.rfind(':');
    if (colonPos == std::string::npos) {
        if (value.empty()) {
            error = "server_addr is empty";
            return false;
        }
        serverHost = value;
        return true;
    }

    std::string host = trim(value.substr(0, colonPos));
    std::string portText = trim(value.substr(colonPos + 1));
    if (host.empty()) {
        error = "server_addr host is empty";
        return false;
    }

    int port = 0;
    if (!parseIntValue(portText, 0, 65535, port, error)) {
        error = "invalid server_addr port: " + portText;
        return false;
    }
    serverHost = host;
    serverPort = static_cast<uint16_t>(port);
    return true;
}

bool parseProtocol(const std::string& value, std::string& protocol, std::string& error)
{
    std::string parsed = toLower(value);
    if (parsed != "tinypb" && parsed != "http") {
        error = "unsupported protocol: " + value;
        return false;
    }
    protocol = parsed;
    return true;
}

bool parseLogLevel(const std::string& value, LogLevel& logLevel, std::string& error)
{
    std::string parsed = toLower(value);
    if (parsed == "debug") {
        logLevel = LogLevel::Debug;
        return true;
    }
    if (parsed == "info") {
        logLevel = LogLevel::Info;
        return true;
    }
    if (parsed == "warn") {
        logLevel = LogLevel::Warn;
        return true;
    }
    if (parsed == "error") {
        logLevel = LogLevel::Error;
        return true;
    }

    error = "unsupported log level: " + value;
    return false;
}

}  // namespace

bool Config::loadFromXml(const std::string& path)
{
    // ifstream 按路径打开 XML 配置文件；失败通常表示路径不存在或当前进程无读取权限。
    std::ifstream input(path);
    if (!input.is_open()) {
        m_lastError = "open config file failed: " + path;
        return false;
    }

    std::ostringstream buffer;
    buffer << input.rdbuf();
    std::string xml = buffer.str();

    std::string serverHost = m_serverHost;
    uint16_t serverPort = m_serverPort;
    std::string protocol = m_protocol;
    int ioThreadNum = m_ioThreadNum;
    int timeoutMs = m_timeoutMs;
    std::string logPath = m_logPath;
    std::string logPrefix = m_logPrefix;
    int64_t logMaxSizeBytes = m_logMaxSizeBytes;
    LogLevel rpcLogLevel = m_rpcLogLevel;
    LogLevel appLogLevel = m_appLogLevel;
    int logSyncIntervalMs = m_logSyncIntervalMs;
    int coroutineStackSizeBytes = m_coroutineStackSizeBytes;
    int coroutinePoolSize = m_coroutinePoolSize;
    int reqIdLen = m_reqIdLen;
    int maxConnectTimeoutMs = m_maxConnectTimeoutMs;
    int timeWheelBucketNum = m_timeWheelBucketNum;
    int timeWheelIntervalSec = m_timeWheelIntervalSec;
    std::string error;

    if (auto value = findTagValue(xml, "server_addr"); value.has_value()) {
        if (!parseServerAddr(*value, serverHost, serverPort, error)) {
            m_lastError = error;
            return false;
        }
    }
    if (auto value = findTagValue(xml, "protocol"); value.has_value()) {
        if (!parseProtocol(*value, protocol, error)) {
            m_lastError = error;
            return false;
        }
    }
    if (auto value = findTagValue(xml, "iothread_num"); value.has_value()) {
        if (!parseIntValue(*value, 0, 1024, ioThreadNum, error)) {
            m_lastError = "invalid iothread_num: " + *value;
            return false;
        }
    }
    if (auto value = findTagValue(xml, "timeout"); value.has_value()) {
        if (!parseIntValue(*value, 1, 24 * 60 * 60 * 1000, timeoutMs, error)) {
            m_lastError = "invalid timeout: " + *value;
            return false;
        }
    }

    if (auto value = findTagValue(xml, "log_path"); value.has_value()) {
        logPath = *value;
    }
    if (auto value = findTagValue(xml, "log_prefix"); value.has_value()) {
        logPrefix = *value;
    }
    if (auto value = findTagValue(xml, "log_max_size"); value.has_value()) {
        if (!parseInt64Value(*value, 0, 1024LL * 1024 * 1024 * 1024, logMaxSizeBytes, error)) {
            m_lastError = "invalid log_max_size: " + *value;
            return false;
        }
    }

    std::optional<std::string> logText = findTagValue(xml, "log_level");
    if (!logText.has_value()) {
        logText = findTagValue(xml, "log");
    }
    if (logText.has_value() && !parseLogLevel(*logText, rpcLogLevel, error)) {
        m_lastError = error;
        return false;
    }
    if (auto value = findTagValue(xml, "app_log_level"); value.has_value()) {
        if (!parseLogLevel(*value, appLogLevel, error)) {
            m_lastError = error;
            return false;
        }
    }
    if (auto value = findTagValue(xml, "log_sync_interval"); value.has_value()) {
        if (!parseIntValue(*value, 1, 24 * 60 * 60 * 1000, logSyncIntervalMs, error)) {
            m_lastError = "invalid log_sync_interval: " + *value;
            return false;
        }
    }
    if (auto value = findTagValue(xml, "cor_stack_size"); value.has_value()) {
        if (!parseIntValue(*value, 1024, 1024 * 1024 * 1024, coroutineStackSizeBytes, error)) {
            m_lastError = "invalid cor_stack_size: " + *value;
            return false;
        }
    }
    if (auto value = findTagValue(xml, "cor_pool_size"); value.has_value()) {
        if (!parseIntValue(*value, 0, 1024 * 1024, coroutinePoolSize, error)) {
            m_lastError = "invalid cor_pool_size: " + *value;
            return false;
        }
    }
    if (auto value = findTagValue(xml, "req_id_len"); value.has_value()) {
        if (!parseIntValue(*value, 1, 1024, reqIdLen, error)) {
            m_lastError = "invalid req_id_len: " + *value;
            return false;
        }
    }
    if (auto value = findTagValue(xml, "max_connect_timeout"); value.has_value()) {
        if (!parseIntValue(*value, 1, 24 * 60 * 60 * 1000, maxConnectTimeoutMs, error)) {
            m_lastError = "invalid max_connect_timeout: " + *value;
            return false;
        }
    }
    if (auto value = findTagValue(xml, "timewheel_bucket_num"); value.has_value()) {
        if (!parseIntValue(*value, 1, 1024 * 1024, timeWheelBucketNum, error)) {
            m_lastError = "invalid timewheel_bucket_num: " + *value;
            return false;
        }
    }
    if (auto value = findTagValue(xml, "timewheel_interval"); value.has_value()) {
        if (!parseIntValue(*value, 1, 24 * 60 * 60, timeWheelIntervalSec, error)) {
            m_lastError = "invalid timewheel_interval: " + *value;
            return false;
        }
    }

    m_serverHost = serverHost;
    m_serverPort = serverPort;
    m_protocol = protocol;
    m_ioThreadNum = ioThreadNum;
    m_timeoutMs = timeoutMs;
    m_logPath = logPath;
    m_logPrefix = logPrefix;
    m_logMaxSizeBytes = logMaxSizeBytes;
    m_rpcLogLevel = rpcLogLevel;
    m_appLogLevel = appLogLevel;
    m_logSyncIntervalMs = logSyncIntervalMs;
    m_coroutineStackSizeBytes = coroutineStackSizeBytes;
    m_coroutinePoolSize = coroutinePoolSize;
    m_reqIdLen = reqIdLen;
    m_maxConnectTimeoutMs = maxConnectTimeoutMs;
    m_timeWheelBucketNum = timeWheelBucketNum;
    m_timeWheelIntervalSec = timeWheelIntervalSec;
    m_lastError.clear();
    return true;
}

const std::string& Config::getServerHost() const
{
    return m_serverHost;
}

uint16_t Config::getServerPort() const
{
    return m_serverPort;
}

const std::string& Config::getProtocol() const
{
    return m_protocol;
}

int Config::getIOThreadNum() const
{
    return m_ioThreadNum;
}

int Config::getTimeoutMs() const
{
    return m_timeoutMs;
}

LogLevel Config::getLogLevel() const
{
    return m_rpcLogLevel;
}

const std::string& Config::getLogPath() const
{
    return m_logPath;
}

const std::string& Config::getLogPrefix() const
{
    return m_logPrefix;
}

int64_t Config::getLogMaxSizeBytes() const
{
    return m_logMaxSizeBytes;
}

LogLevel Config::getRpcLogLevel() const
{
    return m_rpcLogLevel;
}

LogLevel Config::getAppLogLevel() const
{
    return m_appLogLevel;
}

int Config::getLogSyncIntervalMs() const
{
    return m_logSyncIntervalMs;
}

int Config::getCoroutineStackSizeBytes() const
{
    return m_coroutineStackSizeBytes;
}

int Config::getCoroutinePoolSize() const
{
    return m_coroutinePoolSize;
}

int Config::getReqIdLen() const
{
    return m_reqIdLen;
}

int Config::getMaxConnectTimeoutMs() const
{
    return m_maxConnectTimeoutMs;
}

int Config::getTimeWheelBucketNum() const
{
    return m_timeWheelBucketNum;
}

int Config::getTimeWheelIntervalSec() const
{
    return m_timeWheelIntervalSec;
}

const std::string& Config::getLastError() const
{
    return m_lastError;
}

}
