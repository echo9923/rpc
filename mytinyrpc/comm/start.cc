#include "comm/start.h"

namespace tinyrpc {

bool InitConfig(const std::string& path)
{
    return getRuntime().loadConfig(path);
}

bool StartRpcServer()
{
    return getRuntime().createServer();
}

TcpServer::Ptr GetServer()
{
    return getRuntime().getServer();
}

TinyPbDispatcher::Ptr GetTinyPbDispatcher()
{
    return getRuntime().getTinyPbDispatcher();
}

HttpDispatcher::Ptr GetHttpDispatcher()
{
    return getRuntime().getHttpDispatcher();
}

}
