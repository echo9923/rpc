#pragma once

#include "comm/config.h"
#include "net/abstractcodec.h"
#include "net/abstractdispatcher.h"
#include "net/http/httpdispatcher.h"
#include "net/tcpserver.h"
#include "net/tinypb/tinypbdispatcher.h"
#include "net/timer.h"

#include <memory>
#include <string>

namespace tinyrpc {

class RequestContext {
 public:
    const std::string& getTraceId() const;
    const std::string& getReqId() const;
    const std::string& getInterfaceName() const;
    const std::string& getMethodName() const;
    const std::string& getPath() const;
    const std::string& getLocalAddr() const;
    const std::string& getPeerAddr() const;
    ProtocolType getProtocolType() const;
    std::string getProtocolName() const;
    bool hasContext() const;
    std::string toString() const;

    void set(
        const std::string& traceId,
        const std::string& reqId,
        const std::string& interfaceName,
        const std::string& methodName,
        const std::string& localAddr,
        const std::string& peerAddr,
        ProtocolType protocolType,
        const std::string& path = ""
    );
    void set(
        const std::string& reqId,
        const std::string& interfaceName,
        const std::string& methodName,
        const std::string& localAddr,
        const std::string& peerAddr,
        ProtocolType protocolType,
        const std::string& path = ""
    );
    void clear();

 private:
    std::string m_traceId;
    std::string m_reqId;
    std::string m_interfaceName;
    std::string m_methodName;
    std::string m_path;
    std::string m_localAddr;
    std::string m_peerAddr;
    ProtocolType m_protocolType {ProtocolType::TinyPb};
};

// Runtime 保存当前进程启动期的全局对象。
// 当前阶段只管理配置、server、codec 和 dispatcher，后续 request context 会继续扩展。
class Runtime {
 public:
    Config& getConfig();
    const Config& getConfig() const;

    bool loadConfig(const std::string& path);
    bool createServer();

    TcpServer::Ptr getServer() const;
    TinyPbDispatcher::Ptr getTinyPbDispatcher() const;
    HttpDispatcher::Ptr getHttpDispatcher() const;

    bool registerService(std::shared_ptr<google::protobuf::Service> service);
    bool registerHttpServlet(const std::string& path, HttpServlet::Ptr servlet);
    bool addTimerTask(const std::shared_ptr<TimerTask>& task);

    RequestContext& getCurrentRequestContext();
    const RequestContext& getCurrentRequestContext() const;
    void setCurrentRequestContext(
        const std::string& reqId,
        const std::string& interfaceName,
        const std::string& methodName,
        const std::string& localAddr,
        const std::string& peerAddr,
        ProtocolType protocolType,
        const std::string& path = ""
    );
    void setCurrentRequestContext(
        const std::string& traceId,
        const std::string& reqId,
        const std::string& interfaceName,
        const std::string& methodName,
        const std::string& localAddr,
        const std::string& peerAddr,
        ProtocolType protocolType,
        const std::string& path = ""
    );
    void clearCurrentRequestContext();

 private:
    Config m_config;                       // 进程配置对象，解析自配置文件
    TcpServer::Ptr m_server;               // TCP 服务端实例，负责接收并处理连接
    AbstractCodec::Ptr m_codec;            // 协议编解码器，负责请求/响应的序列化与反序列化
    AbstractDispatcher::Ptr m_dispatcher;  // 通用请求分发器基类指针，按协议类型分发请求
    TinyPbDispatcher::Ptr m_tinyPbDispatcher;  // TinyPb 协议请求分发器，匹配并调用对应的 RPC service
    HttpDispatcher::Ptr m_httpDispatcher;  // HTTP 协议请求分发器，按 path 调用对应的 HttpServlet
};

Runtime& getRuntime();

}
