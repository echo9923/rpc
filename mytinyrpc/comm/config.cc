#include "comm/config.h"

namespace tinyrpc {

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

}
