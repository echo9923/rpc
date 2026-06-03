#include "comm/start.h"
#include "comm/log.h"

namespace tinyrpc {

bool InitConfig(const std::string& path)
{
    return getRuntime().loadConfig(path);
}

bool StartRpcServer()
{
    const Config& config = getRuntime().getConfig();
    bool loggerReady = Logger::init(
        config.getLogPath(),
        config.getLogPrefix(),
        config.getRpcLogLevel(),
        config.getAppLogLevel(),
        true,
        config.getLogSyncIntervalMs(),
        config.getLogMaxSizeBytes()
    );
    bool serverReady = getRuntime().createServer();
    if (!serverReady && loggerReady) {
        Logger::shutdown();
    }
    return serverReady;
}

Config& GetConfig()
{
    return getRuntime().getConfig();
}

const Config& GetConstConfig()
{
    return getRuntime().getConfig();
}

TcpServer::Ptr GetServer()
{
    return getRuntime().getServer();
}

int GetIOThreadPoolSize()
{
    return getRuntime().getConfig().getIOThreadNum();
}

bool AddTimerTask(const std::shared_ptr<TimerTask>& task)
{
    return getRuntime().addTimerTask(task);
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
