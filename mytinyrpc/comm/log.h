#pragma once

#include <string>

namespace tinyrpc {

enum class LogLevel {
    Debug = 1,
    Info = 2,
    Warn = 3,
    Error = 4
};

class Logger {
 public:
    // 初始化文件日志。async 为 true 时写入后台队列，flush()/shutdown() 会等待队列落盘。
    static bool init(const std::string& path, LogLevel level = LogLevel::Debug, bool async = false);

    // 关闭文件日志并恢复默认控制台输出，供测试和进程退出时清理资源。
    static void shutdown();

    // 调整最小输出级别，小于该级别的日志会被过滤。
    static void setLevel(LogLevel level);

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
        const std::string& msgReq
    );

 private:
    static const char* levelToString(LogLevel level);
};

}

#define DebugLog(msg) tinyrpc::Logger::log(tinyrpc::LogLevel::Debug, __FILE__, __LINE__, msg)
#define InfoLog(msg)  tinyrpc::Logger::log(tinyrpc::LogLevel::Info,  __FILE__, __LINE__, msg)
#define WarnLog(msg)  tinyrpc::Logger::log(tinyrpc::LogLevel::Warn,  __FILE__, __LINE__, msg)
#define ErrorLog(msg) tinyrpc::Logger::log(tinyrpc::LogLevel::Error, __FILE__, __LINE__, msg)

#define DebugLogWithMsgReq(msgReq, msg) \
    tinyrpc::Logger::log(tinyrpc::LogLevel::Debug, __FILE__, __LINE__, msg, msgReq)
#define InfoLogWithMsgReq(msgReq, msg) \
    tinyrpc::Logger::log(tinyrpc::LogLevel::Info, __FILE__, __LINE__, msg, msgReq)
#define WarnLogWithMsgReq(msgReq, msg) \
    tinyrpc::Logger::log(tinyrpc::LogLevel::Warn, __FILE__, __LINE__, msg, msgReq)
#define ErrorLogWithMsgReq(msgReq, msg) \
    tinyrpc::Logger::log(tinyrpc::LogLevel::Error, __FILE__, __LINE__, msg, msgReq)
