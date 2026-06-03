#pragma once

#include "comm/runtime.h"

#include <memory>
#include <string>

namespace tinyrpc {

bool InitConfig(const std::string& path);
bool StartRpcServer();
Config& GetConfig();
const Config& GetConstConfig();
TcpServer::Ptr GetServer();
int GetIOThreadPoolSize();
bool AddTimerTask(const std::shared_ptr<TimerTask>& task);
TinyPbDispatcher::Ptr GetTinyPbDispatcher();
HttpDispatcher::Ptr GetHttpDispatcher();

template <typename ServiceType>
bool RegisterService()
{
    return getRuntime().registerService(std::make_shared<ServiceType>());
}

template <typename ServletType>
bool RegisterHttpServlet(const std::string& path)
{
    return getRuntime().registerHttpServlet(path, std::make_shared<ServletType>());
}

}

#define REGISTER_SERVICE(ServiceType) tinyrpc::RegisterService<ServiceType>()
#define REGISTER_HTTP_SERVLET(path, ServletType) tinyrpc::RegisterHttpServlet<ServletType>(path)
