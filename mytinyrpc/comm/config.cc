#include "comm/config.h"

#include <algorithm>
#include <cctype>
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
    if (!parseIntValue(portText, 1, 65535, port, error)) {
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
    LogLevel logLevel = m_logLevel;
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

    std::optional<std::string> logText = findTagValue(xml, "log_level");
    if (!logText.has_value()) {
        logText = findTagValue(xml, "log");
    }
    if (logText.has_value() && !parseLogLevel(*logText, logLevel, error)) {
        m_lastError = error;
        return false;
    }

    m_serverHost = serverHost;
    m_serverPort = serverPort;
    m_protocol = protocol;
    m_ioThreadNum = ioThreadNum;
    m_timeoutMs = timeoutMs;
    m_logLevel = logLevel;
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
    return m_logLevel;
}

const std::string& Config::getLastError() const
{
    return m_lastError;
}

}
