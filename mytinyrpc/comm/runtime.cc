#include "comm/runtime.h"
#include "comm/log.h"
#include "net/http/httpcodec.h"
#include "net/netaddress.h"
#include "net/tinypb/tinypbcodec.h"

namespace tinyrpc {

Config& Runtime::getConfig()
{
    return m_config;
}

const Config& Runtime::getConfig() const
{
    return m_config;
}

bool Runtime::loadConfig(const std::string& path)
{
    return m_config.loadFromXml(path);
}

bool Runtime::createServer()
{
    m_server.reset();
    m_codec.reset();
    m_dispatcher.reset();
    m_tinyPbDispatcher.reset();
    m_httpDispatcher.reset();

    if (m_config.getProtocol() == "tinypb") {
        m_codec = std::make_shared<TinyPbCodec>();
        m_tinyPbDispatcher = std::make_shared<TinyPbDispatcher>();
        m_dispatcher = m_tinyPbDispatcher;
    } else if (m_config.getProtocol() == "http") {
        m_codec = std::make_shared<HttpCodec>();
        m_httpDispatcher = std::make_shared<HttpDispatcher>();
        m_dispatcher = m_httpDispatcher;
    } else {
        ErrorLog("Runtime createServer failed, unsupported protocol = " + m_config.getProtocol());
        return false;
    }

    m_server = std::make_shared<TcpServer>(
        IPAddress(m_config.getServerHost(), m_config.getServerPort()),
        m_codec,
        m_dispatcher
    );
    m_server->setIOThreadNum(m_config.getIOThreadNum());
    return m_server->init();
}

TcpServer::Ptr Runtime::getServer() const
{
    return m_server;
}

TinyPbDispatcher::Ptr Runtime::getTinyPbDispatcher() const
{
    return m_tinyPbDispatcher;
}

HttpDispatcher::Ptr Runtime::getHttpDispatcher() const
{
    return m_httpDispatcher;
}

bool Runtime::registerService(std::shared_ptr<google::protobuf::Service> service)
{
    if (m_server == nullptr) {
        ErrorLog("Runtime registerService failed, server is null");
        return false;
    }
    return m_server->registerService(std::move(service));
}

bool Runtime::registerHttpServlet(const std::string& path, HttpServlet::Ptr servlet)
{
    if (m_httpDispatcher == nullptr) {
        ErrorLog("Runtime registerHttpServlet failed, dispatcher is null");
        return false;
    }
    return m_httpDispatcher->registerServlet(path, std::move(servlet));
}

Runtime& getRuntime()
{
    static Runtime runtime;
    return runtime;
}

}
