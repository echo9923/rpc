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
