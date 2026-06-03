#pragma once

#include <string>

namespace tinyrpc {

enum class LogLevel {
    Debug = 1,
    Info = 2,
    Warn = 3,
    Error = 4
};

enum class LogType {
    RpcLog = 1,
    AppLog = 2,
};

struct LogEvent {
    LogType m_type {LogType::RpcLog};
    LogLevel m_level {LogLevel::Debug};
    std::string m_time;
    int m_pid {0};
    std::string m_threadId;
    int m_coroutineId {0};
    const char *m_file {nullptr};
    int m_line {0};
    const char *m_function {nullptr};
    std::string m_reqId;
    std::string m_message;
};

class Logger {
 public:
    // 初始化文件日志。async 为 true 时写入后台队列，flush()/shutdown() 会等待队列落盘。
    static bool init(const std::string& path, LogLevel level = LogLevel::Debug, bool async = false);
    static bool init(
        const std::string& logPath,
        const std::string& prefix,
        LogLevel rpcLevel,
        LogLevel appLevel,
        bool async = false
    );

    // 关闭文件日志并恢复默认控制台输出，供测试和进程退出时清理资源。
    static void shutdown();

    // 调整最小输出级别，小于该级别的日志会被过滤。
    static void setLevel(LogLevel level);
    static void setLevel(LogType type, LogLevel level);

    // enabled=false 时直接丢弃日志，用于临时关闭日志输出。
    static void setEnabled(bool enabled);

    // 同步模式刷新文件缓冲；异步模式等待队列写完后刷新文件。
    static void flush();

    static void log(LogLevel level, const char* file, int line, const std::string& msg);
    static void log(
        LogLevel level,
        const char* file,
        int line,
        const std::string& msg,
        const std::string& reqId
    );
    static void log(
        LogLevel level,
        const char* file,
        int line,
        const char* function,
        const std::string& msg
    );
    static void log(
        LogLevel level,
        const char* file,
        int line,
        const char* function,
        const std::string& msg,
        const std::string& reqId
    );
    static void log(
        LogType type,
        LogLevel level,
        const char* file,
        int line,
        const char* function,
        const std::string& msg
    );
    static void log(
        LogType type,
        LogLevel level,
        const char* file,
        int line,
        const char* function,
        const std::string& msg,
        const std::string& reqId
    );

 private:
    static const char* levelToString(LogLevel level);
};

}

#define DebugLog(msg) tinyrpc::Logger::log(tinyrpc::LogType::RpcLog, tinyrpc::LogLevel::Debug, __FILE__, __LINE__, __FUNCTION__, msg)
#define InfoLog(msg)  tinyrpc::Logger::log(tinyrpc::LogType::RpcLog, tinyrpc::LogLevel::Info,  __FILE__, __LINE__, __FUNCTION__, msg)
#define WarnLog(msg)  tinyrpc::Logger::log(tinyrpc::LogType::RpcLog, tinyrpc::LogLevel::Warn,  __FILE__, __LINE__, __FUNCTION__, msg)
#define ErrorLog(msg) tinyrpc::Logger::log(tinyrpc::LogType::RpcLog, tinyrpc::LogLevel::Error, __FILE__, __LINE__, __FUNCTION__, msg)

#define AppDebugLog(msg) tinyrpc::Logger::log(tinyrpc::LogType::AppLog, tinyrpc::LogLevel::Debug, __FILE__, __LINE__, __FUNCTION__, msg)
#define AppInfoLog(msg)  tinyrpc::Logger::log(tinyrpc::LogType::AppLog, tinyrpc::LogLevel::Info,  __FILE__, __LINE__, __FUNCTION__, msg)
#define AppWarnLog(msg)  tinyrpc::Logger::log(tinyrpc::LogType::AppLog, tinyrpc::LogLevel::Warn,  __FILE__, __LINE__, __FUNCTION__, msg)
#define AppErrorLog(msg) tinyrpc::Logger::log(tinyrpc::LogType::AppLog, tinyrpc::LogLevel::Error, __FILE__, __LINE__, __FUNCTION__, msg)

#define DebugLogWithReqId(reqId, msg) \
    tinyrpc::Logger::log(tinyrpc::LogType::RpcLog, tinyrpc::LogLevel::Debug, __FILE__, __LINE__, __FUNCTION__, msg, reqId)
#define InfoLogWithReqId(reqId, msg) \
    tinyrpc::Logger::log(tinyrpc::LogType::RpcLog, tinyrpc::LogLevel::Info, __FILE__, __LINE__, __FUNCTION__, msg, reqId)
#define WarnLogWithReqId(reqId, msg) \
    tinyrpc::Logger::log(tinyrpc::LogType::RpcLog, tinyrpc::LogLevel::Warn, __FILE__, __LINE__, __FUNCTION__, msg, reqId)
#define ErrorLogWithReqId(reqId, msg) \
    tinyrpc::Logger::log(tinyrpc::LogType::RpcLog, tinyrpc::LogLevel::Error, __FILE__, __LINE__, __FUNCTION__, msg, reqId)
