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

std::optional<std::string> findSection(const std::string& xml, const std::string& sectionName)
{
    return findTagValue(xml, sectionName);
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

bool parseOptionalIntField(
    const std::optional<std::string>& section,
    const std::string& sectionName,
    const std::string& fieldName,
    int minValue,
    int maxValue,
    int& result,
    std::string& error
)
{
    if (!section.has_value()) {
        return true;
    }
    auto value = findTagValue(*section, fieldName);
    if (!value.has_value()) {
        return true;
    }
    if (!parseIntValue(*value, minValue, maxValue, result, error)) {
        error = "invalid " + sectionName + "." + fieldName + ": " + *value;
        return false;
    }
    return true;
}

bool parseOptionalInt64Field(
    const std::optional<std::string>& section,
    const std::string& sectionName,
    const std::string& fieldName,
    int64_t minValue,
    int64_t maxValue,
    int64_t& result,
    std::string& error
)
{
    if (!section.has_value()) {
        return true;
    }
    auto value = findTagValue(*section, fieldName);
    if (!value.has_value()) {
        return true;
    }
    if (!parseInt64Value(*value, minValue, maxValue, result, error)) {
        error = "invalid " + sectionName + "." + fieldName + ": " + *value;
        return false;
    }
    return true;
}

bool parseOptionalLogLevelField(
    const std::optional<std::string>& section,
    const std::string& sectionName,
    const std::string& fieldName,
    LogLevel& result,
    std::string& error
)
{
    if (!section.has_value()) {
        return true;
    }
    auto value = findTagValue(*section, fieldName);
    if (!value.has_value()) {
        return true;
    }
    if (!parseLogLevel(*value, result, error)) {
        error = "invalid " + sectionName + "." + fieldName + ": " + *value;
        return false;
    }
    return true;
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

    auto serverSection = findSection(xml, "server");
    if (!serverSection.has_value()) {
        m_lastError = "missing server section";
        return false;
    }

    if (auto value = findTagValue(*serverSection, "host"); value.has_value()) {
        if (value->empty()) {
            m_lastError = "invalid server.host: empty";
            return false;
        }
        serverHost = *value;
    }
    if (auto value = findTagValue(*serverSection, "port"); value.has_value()) {
        int port = serverPort;
        if (!parseIntValue(*value, 0, 65535, port, error)) {
            m_lastError = "invalid server.port: " + *value;
            return false;
        }
        serverPort = static_cast<uint16_t>(port);
    }
    if (auto value = findTagValue(*serverSection, "protocol"); value.has_value()) {
        if (!parseProtocol(*value, protocol, error)) {
            m_lastError = "invalid server.protocol: " + *value;
            return false;
        }
    }

    auto networkSection = findSection(xml, "network");
    if (!parseOptionalIntField(networkSection, "network", "iothread_num", 0, 1024, ioThreadNum, error)
        || !parseOptionalIntField(networkSection, "network", "timeout_ms", 1, 24 * 60 * 60 * 1000, timeoutMs, error)
        || !parseOptionalIntField(
            networkSection,
            "network",
            "max_connect_timeout_ms",
            1,
            24 * 60 * 60 * 1000,
            maxConnectTimeoutMs,
            error)) {
        m_lastError = error;
        return false;
    }

    auto logSection = findSection(xml, "log");
    if (logSection.has_value()) {
        if (auto value = findTagValue(*logSection, "path"); value.has_value()) {
            logPath = *value;
        }
        if (auto value = findTagValue(*logSection, "prefix"); value.has_value()) {
            logPrefix = *value;
        }
    }
    int64_t logMaxSizeMb = logMaxSizeBytes / (1024 * 1024);
    if (!parseOptionalInt64Field(logSection, "log", "max_size_mb", 0, 1024 * 1024, logMaxSizeMb, error)
        || !parseOptionalLogLevelField(logSection, "log", "rpc_level", rpcLogLevel, error)
        || !parseOptionalLogLevelField(logSection, "log", "app_level", appLogLevel, error)
        || !parseOptionalIntField(
            logSection,
            "log",
            "sync_interval_ms",
            1,
            24 * 60 * 60 * 1000,
            logSyncIntervalMs,
            error)) {
        m_lastError = error;
        return false;
    }
    logMaxSizeBytes = logMaxSizeMb * 1024 * 1024;

    auto coroutineSection = findSection(xml, "coroutine");
    int coroutineStackSizeKb = coroutineStackSizeBytes / 1024;
    if (!parseOptionalIntField(coroutineSection, "coroutine", "stack_size_kb", 1, 1024 * 1024, coroutineStackSizeKb, error)
        || !parseOptionalIntField(coroutineSection, "coroutine", "pool_size", 0, 1024 * 1024, coroutinePoolSize, error)) {
        m_lastError = error;
        return false;
    }
    coroutineStackSizeBytes = coroutineStackSizeKb * 1024;

    auto timeWheelSection = findSection(xml, "timewheel");
    if (!parseOptionalIntField(timeWheelSection, "timewheel", "bucket_num", 1, 1024 * 1024, timeWheelBucketNum, error)
        || !parseOptionalIntField(timeWheelSection, "timewheel", "interval_sec", 1, 24 * 60 * 60, timeWheelIntervalSec, error)) {
        m_lastError = error;
        return false;
    }

    auto rpcSection = findSection(xml, "rpc");
    if (!parseOptionalIntField(rpcSection, "rpc", "req_id_len", 1, 1024, reqIdLen, error)) {
        m_lastError = error;
        return false;
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
