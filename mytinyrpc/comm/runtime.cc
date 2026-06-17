#include "comm/runtime.h"
#include "comm/log.h"
#include "comm/reqid.h"
#include "net/http/httpcodec.h"
#include "net/netaddress.h"
#include "net/tinypb/tinypbcodec.h"

#include <sstream>

namespace tinyrpc {

namespace {

thread_local RequestContext t_requestContext;

std::string protocolTypeToString(ProtocolType protocolType)
{
    switch (protocolType) {
    case ProtocolType::TinyPb:
        return "tinypb";
    case ProtocolType::Http:
        return "http";
    default:
        return "unknown";
    }
}

std::string makeTraceId(const std::string& traceId, const std::string& reqId)
{
    if (!traceId.empty()) {
        return traceId;
    }
    if (!reqId.empty()) {
        return reqId;
    }
    return ReqIdUtil::genReqId();
}

}  // namespace

const std::string& RequestContext::getTraceId() const
{
    return m_traceId;
}

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

const std::string& RequestContext::getPath() const
{
    return m_path;
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

std::string RequestContext::getProtocolName() const
{
    if (!hasContext()) {
        return "";
    }
    return protocolTypeToString(m_protocolType);
}

bool RequestContext::hasContext() const
{
    return !m_traceId.empty()
        || !m_reqId.empty()
        || !m_interfaceName.empty()
        || !m_methodName.empty()
        || !m_path.empty()
        || !m_localAddr.empty()
        || !m_peerAddr.empty();
}

std::string RequestContext::toString() const
{
    std::ostringstream stream;
    stream << "traceId=" << m_traceId
           << " reqId=" << m_reqId
           << " interface=" << m_interfaceName
           << " method=" << m_methodName
           << " path=" << m_path
           << " local=" << m_localAddr
           << " peer=" << m_peerAddr
           << " protocol=" << getProtocolName();
    return stream.str();
}

void RequestContext::set(
    const std::string& traceId,
    const std::string& reqId,
    const std::string& interfaceName,
    const std::string& methodName,
    const std::string& localAddr,
    const std::string& peerAddr,
    ProtocolType protocolType,
    const std::string& path
)
{
    m_traceId = makeTraceId(traceId, reqId);
    m_reqId = reqId;
    m_interfaceName = interfaceName;
    m_methodName = methodName;
    m_path = path;
    m_localAddr = localAddr;
    m_peerAddr = peerAddr;
    m_protocolType = protocolType;
}

void RequestContext::set(
    const std::string& reqId,
    const std::string& interfaceName,
    const std::string& methodName,
    const std::string& localAddr,
    const std::string& peerAddr,
    ProtocolType protocolType,
    const std::string& path
)
{
    set("", reqId, interfaceName, methodName, localAddr, peerAddr, protocolType, path);
}

void RequestContext::clear()
{
    m_traceId.clear();
    m_reqId.clear();
    m_interfaceName.clear();
    m_methodName.clear();
    m_path.clear();
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

// 创建 TCP 服务器
// 该方法会根据配置文件中指定的协议类型（tinypb 或 http），
// 创建相应的编解码器（Codec）和分发器（Dispatcher），并初始化 TCP 服务器。
// 返回值：服务器初始化成功返回 true，否则返回 false。
bool Runtime::createServer()
{
    // 先重置所有服务器相关的成员变量，确保处于干净状态
    m_server.reset();          // 重置 TCP 服务器对象
    m_codec.reset();           // 重置协议编解码器
    m_dispatcher.reset();      // 重置通用分发器
    m_tinyPbDispatcher.reset();// 重置 TinyPb 协议分发器
    m_httpDispatcher.reset();  // 重置 HTTP 协议分发器

    // 根据配置中的协议类型选择对应的编解码器和分发器
    if (m_config.getProtocol() == "tinypb") {
        // TinyPb 协议分支：使用 TinyPb 编解码器和分发器
        m_codec = std::make_shared<TinyPbCodec>();
        m_tinyPbDispatcher = std::make_shared<TinyPbDispatcher>();
        m_dispatcher = m_tinyPbDispatcher;  // 通用分发器指向 TinyPb 分发器
    } else if (m_config.getProtocol() == "http") {
        // HTTP 协议分支：使用 HTTP 编解码器和分发器
        m_codec = std::make_shared<HttpCodec>();
        m_httpDispatcher = std::make_shared<HttpDispatcher>();
        m_dispatcher = m_httpDispatcher;    // 通用分发器指向 HTTP 分发器
    } else {
        // 不支持的协议类型，记录错误日志并返回失败
        ErrorLog("Runtime createServer failed, unsupported protocol = " + m_config.getProtocol());
        return false;
    }

    // 使用配置中的主机地址和端口号创建 TCP 服务器对象，
    // 并传入前面创建的编解码器和分发器。
    m_server = std::make_shared<TcpServer>(
        IPAddress(m_config.getServerHost(), m_config.getServerPort()),
        m_codec,
        m_dispatcher
    );
    // 设置服务器使用的 IO 线程数量
    m_server->setIOThreadNum(m_config.getIOThreadNum());
    // 初始化服务器（如绑定端口、启动监听等），并返回初始化结果
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
    ProtocolType protocolType,
    const std::string& path
)
{
    t_requestContext.set(reqId, interfaceName, methodName, localAddr, peerAddr, protocolType, path);
}

void Runtime::setCurrentRequestContext(
    const std::string& traceId,
    const std::string& reqId,
    const std::string& interfaceName,
    const std::string& methodName,
    const std::string& localAddr,
    const std::string& peerAddr,
    ProtocolType protocolType,
    const std::string& path
)
{
    t_requestContext.set(traceId, reqId, interfaceName, methodName, localAddr, peerAddr, protocolType, path);
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
