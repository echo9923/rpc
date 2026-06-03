#pragma once

#include "comm/config.h"
#include "net/abstractcodec.h"
#include "net/abstractdispatcher.h"
#include "net/http/httpdispatcher.h"
#include "net/tcpserver.h"
#include "net/tinypb/tinypbdispatcher.h"

#include <memory>
#include <string>

namespace tinyrpc {

class RequestContext {
 public:
    const std::string& getReqId() const;
    const std::string& getMethodName() const;
    const std::string& getLocalAddr() const;
    const std::string& getPeerAddr() const;

    void set(
        const std::string& reqId,
        const std::string& methodName,
        const std::string& localAddr,
        const std::string& peerAddr
    );
    void clear();

 private:
    std::string m_reqId;
    std::string m_methodName;
    std::string m_localAddr;
    std::string m_peerAddr;
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

    RequestContext& getCurrentRequestContext();
    const RequestContext& getCurrentRequestContext() const;
    void setCurrentRequestContext(
        const std::string& reqId,
        const std::string& methodName,
        const std::string& localAddr,
        const std::string& peerAddr
    );
    void clearCurrentRequestContext();

 private:
    Config m_config;
    TcpServer::Ptr m_server;
    AbstractCodec::Ptr m_codec;
    AbstractDispatcher::Ptr m_dispatcher;
    TinyPbDispatcher::Ptr m_tinyPbDispatcher;
    HttpDispatcher::Ptr m_httpDispatcher;
};

Runtime& getRuntime();

}
