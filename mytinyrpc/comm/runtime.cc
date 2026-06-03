#include "comm/runtime.h"
#include "comm/log.h"
#include "net/http/httpcodec.h"
#include "net/netaddress.h"
#include "net/tinypb/tinypbcodec.h"

namespace tinyrpc {

namespace {

thread_local RequestContext t_requestContext;

}  // namespace

const std::string& RequestContext::getReqId() const
{
    return m_reqId;
}

const std::string& RequestContext::getInterfaceName() const
{
    return m_interfaceName;
}

const std::string& RequestContext::getMethodName() const
{
    return m_methodName;
}

const std::string& RequestContext::getLocalAddr() const
{
    return m_localAddr;
}

const std::string& RequestContext::getPeerAddr() const
{
    return m_peerAddr;
}

ProtocolType RequestContext::getProtocolType() const
{
    return m_protocolType;
}

void RequestContext::set(
    const std::string& reqId,
    const std::string& interfaceName,
    const std::string& methodName,
    const std::string& localAddr,
    const std::string& peerAddr,
    ProtocolType protocolType
)
{
    m_reqId = reqId;
    m_interfaceName = interfaceName;
    m_methodName = methodName;
    m_localAddr = localAddr;
    m_peerAddr = peerAddr;
    m_protocolType = protocolType;
}

void RequestContext::clear()
{
    m_reqId.clear();
    m_interfaceName.clear();
    m_methodName.clear();
    m_localAddr.clear();
    m_peerAddr.clear();
    m_protocolType = ProtocolType::TinyPb;
}

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

bool Runtime::addTimerTask(const std::shared_ptr<TimerTask>& task)
{
    if (m_server == nullptr || task == nullptr) {
        return false;
    }
    return m_server->addTimerTask(task);
}

RequestContext& Runtime::getCurrentRequestContext()
{
    return t_requestContext;
}

const RequestContext& Runtime::getCurrentRequestContext() const
{
    return t_requestContext;
}

void Runtime::setCurrentRequestContext(
    const std::string& reqId,
    const std::string& interfaceName,
    const std::string& methodName,
    const std::string& localAddr,
    const std::string& peerAddr,
    ProtocolType protocolType
)
{
    t_requestContext.set(reqId, interfaceName, methodName, localAddr, peerAddr, protocolType);
}

void Runtime::clearCurrentRequestContext()
{
    t_requestContext.clear();
}

Runtime& getRuntime()
{
    static Runtime runtime;
    return runtime;
}

}
