#include "comm/log.h"
#include "comm/runtime.h"
#include "coroutine/coroutine.h"

#include <condition_variable>
#include <chrono>
#include <ctime>
#include <deque>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <mutex>
#include <sstream>
#include <thread>
#include <unistd.h>

namespace tinyrpc {

namespace {

class LoggerState {
 public:
    ~LoggerState()
    {
        shutdown();
    }

    bool init(const std::string& path, LogLevel level, bool async)
    {
        shutdown();

        std::lock_guard<std::mutex> lock(m_mutex);
        m_rpcSink.open(path, level);
        if (!m_rpcSink.m_file.is_open()) {
            m_consoleMode = true;
            return false;
        }

        m_appSink.reset(LogLevel::Debug);
        m_enabled = true;
        m_consoleMode = false;
        m_async = async;
        m_stopping = false;
        if (m_async) {
            m_worker = std::thread([this]() {
                runWorker();
            });
        }
        return true;
    }

    bool init(
        const std::string& logPath,
        const std::string& prefix,
        LogLevel rpcLevel,
        LogLevel appLevel,
        bool async)
    {
        shutdown();

        std::lock_guard<std::mutex> lock(m_mutex);
        // std::filesystem::create_directories 递归创建日志目录；目录已存在时不会报错。
        std::filesystem::create_directories(logPath);
        std::string rpcPath = logPath + "/" + prefix + "_rpc.log";
        std::string appPath = logPath + "/" + prefix + "_app.log";
        m_rpcSink.open(rpcPath, rpcLevel);
        m_appSink.open(appPath, appLevel);
        if (!m_rpcSink.m_file.is_open() || !m_appSink.m_file.is_open()) {
            m_rpcSink.close();
            m_appSink.close();
            m_consoleMode = true;
            return false;
        }

        m_enabled = true;
        m_consoleMode = false;
        m_async = async;
        m_stopping = false;
        if (m_async) {
            m_worker = std::thread([this]() {
                runWorker();
            });
        }
        return true;
    }

    void shutdown()
    {
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            if (m_async) {
                m_stopping = true;
                m_condition.notify_all();
            }
        }

        if (m_worker.joinable()) {
            m_worker.join();
        }

        std::lock_guard<std::mutex> lock(m_mutex);
        flushFilesLocked();
        m_rpcSink.close();
        m_appSink.close();
        m_messages.clear();
        m_pendingCount = 0;
        m_async = false;
        m_stopping = false;
        m_enabled = true;
        m_rpcSink.reset(LogLevel::Debug);
        m_appSink.reset(LogLevel::Debug);
        m_consoleMode = true;
    }

    void setLevel(LogLevel level)
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_rpcSink.m_level = level;
    }

    void setLevel(LogType type, LogLevel level)
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        getSinkLocked(type).m_level = level;
    }

    void setEnabled(bool enabled)
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_enabled = enabled;
    }

    void flush()
    {
        std::unique_lock<std::mutex> lock(m_mutex);
        if (m_async) {
            m_condition.wait(lock, [this]() {
                return m_messages.empty() && m_pendingCount == 0;
            });
        }
        flushFilesLocked();
    }

    void write(LogType type, LogLevel level, const std::string& line)
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        Sink& sink = getSinkLocked(type);
        if (!m_enabled || level < sink.m_level) {
            return;
        }

        if (m_consoleMode) {
            std::cout << line << std::endl;
            return;
        }

        if (!sink.m_file.is_open()) {
            return;
        }

        if (m_async) {
            m_messages.push_back(QueuedLine {type, line});
            m_condition.notify_one();
            return;
        }

        // std::ofstream::operator<< 写入文件缓冲；flush() 或 shutdown() 负责强制刷盘。
        sink.m_file << line << std::endl;
    }

 private:
    struct Sink {
        void open(const std::string& path, LogLevel level)
        {
            close();
            m_path = path;
            m_level = level;
            m_file.open(path, std::ios::out | std::ios::app);
        }

        void close()
        {
            if (m_file.is_open()) {
                m_file.close();
            }
            m_path.clear();
        }

        void reset(LogLevel level)
        {
            close();
            m_level = level;
        }

        std::string m_path;
        std::ofstream m_file;
        LogLevel m_level {LogLevel::Debug};
    };

    struct QueuedLine {
        LogType m_type {LogType::RpcLog};
        std::string m_line;
    };

    void runWorker()
    {
        while (true) {
            QueuedLine queuedLine;
            {
                std::unique_lock<std::mutex> lock(m_mutex);
                m_condition.wait(lock, [this]() {
                    return m_stopping || !m_messages.empty();
                });
                if (m_stopping && m_messages.empty()) {
                    m_condition.notify_all();
                    return;
                }

                queuedLine = std::move(m_messages.front());
                m_messages.pop_front();
                ++m_pendingCount;
            }

            {
                std::lock_guard<std::mutex> lock(m_mutex);
                Sink& sink = getSinkLocked(queuedLine.m_type);
                if (sink.m_file.is_open()) {
                    // 后台线程串行写文件，避免业务线程直接阻塞在磁盘 I/O 上。
                    sink.m_file << queuedLine.m_line << std::endl;
                }
                --m_pendingCount;
                if (m_messages.empty() && m_pendingCount == 0) {
                    m_condition.notify_all();
                }
            }
        }
    }

    Sink& getSinkLocked(LogType type)
    {
        if (type == LogType::AppLog) {
            return m_appSink;
        }
        return m_rpcSink;
    }

    void flushFilesLocked()
    {
        if (m_rpcSink.m_file.is_open()) {
            m_rpcSink.m_file.flush();
        }
        if (m_appSink.m_file.is_open()) {
            m_appSink.m_file.flush();
        }
    }

    std::mutex m_mutex;
    std::condition_variable m_condition;
    Sink m_rpcSink;
    Sink m_appSink;
    std::deque<QueuedLine> m_messages;
    std::thread m_worker;
    size_t m_pendingCount {0};
    bool m_async {false};
    bool m_stopping {false};
    bool m_enabled {true};
    bool m_consoleMode {true};
};

LoggerState& getLoggerState()
{
    static LoggerState state;
    return state;
}

const char* logLevelToString(LogLevel level)
{
    switch (level) {
        case LogLevel::Debug:
            return "DEBUG";
        case LogLevel::Info:
            return "INFO";
        case LogLevel::Warn:
            return "WARN";
        case LogLevel::Error:
            return "ERROR";
        default:
            return "UNKNOWN";
    }
}

const char* logTypeToString(LogType type)
{
    switch (type) {
        case LogType::RpcLog:
            return "RPC";
        case LogType::AppLog:
            return "APP";
        default:
            return "UNKNOWN";
    }
}

std::string currentTimeString()
{
    auto now = std::chrono::system_clock::now();
    auto tt = std::chrono::system_clock::to_time_t(now);

    std::tm tm {};
    // localtime_r 将 time_t 转为本地时间结构体，第二个参数是线程私有输出缓冲。
    localtime_r(&tt, &tm);

    char timebuf[32];
    strftime(timebuf, sizeof(timebuf), "%Y-%m-%d %H:%M:%S", &tm);
    return timebuf;
}

std::string formatThreadId()
{
    std::ostringstream stream;
    stream << std::this_thread::get_id();
    return stream.str();
}

int currentCoroutineId()
{
    Coroutine* current = Coroutine::getCurrentCoroutine();
    if (current == nullptr || Coroutine::isMainCoroutine()) {
        return 0;
    }
    return current->getId();
}

LogEvent makeLogEvent(
    LogType type,
    LogLevel level,
    const char* file,
    int line,
    const char* function,
    const std::string& msg,
    const std::string& reqId)
{
    LogEvent event;
    event.m_type = type;
    event.m_level = level;
    event.m_time = currentTimeString();
    // getpid 读取当前进程 ID，用于多进程日志排查。
    event.m_pid = static_cast<int>(getpid());
    event.m_threadId = formatThreadId();
    event.m_coroutineId = currentCoroutineId();
    event.m_file = file;
    event.m_line = line;
    event.m_function = function;
    event.m_reqId = reqId;
    event.m_message = msg;
    return event;
}

std::string formatLogEvent(const LogEvent& event)
{
    std::ostringstream stream;
    stream << "[" << event.m_time << "] "
           << "[" << logTypeToString(event.m_type) << "] "
           << "[" << logLevelToString(event.m_level) << "] "
           << "[pid=" << event.m_pid << "] "
           << "[tid=" << event.m_threadId << "] "
           << "[co=" << event.m_coroutineId << "] "
           << "[reqId=" << event.m_reqId << "] "
           << "[" << (event.m_file == nullptr ? "" : event.m_file) << ":" << event.m_line << "] "
           << "[func=" << (event.m_function == nullptr ? "" : event.m_function) << "] "
           << event.m_message;
    return stream.str();
}

}  // namespace

const char* Logger::levelToString(LogLevel level)
{
    return logLevelToString(level);
}

bool Logger::init(const std::string& path, LogLevel level, bool async)
{
    return getLoggerState().init(path, level, async);
}

bool Logger::init(
    const std::string& logPath,
    const std::string& prefix,
    LogLevel rpcLevel,
    LogLevel appLevel,
    bool async)
{
    return getLoggerState().init(logPath, prefix, rpcLevel, appLevel, async);
}

void Logger::shutdown()
{
    getLoggerState().shutdown();
}

void Logger::setLevel(LogLevel level)
{
    getLoggerState().setLevel(level);
}

void Logger::setLevel(LogType type, LogLevel level)
{
    getLoggerState().setLevel(type, level);
}

void Logger::setEnabled(bool enabled)
{
    getLoggerState().setEnabled(enabled);
}

void Logger::flush()
{
    getLoggerState().flush();
}

void Logger::log(LogLevel level, const char* file, int line, const std::string& msg)
{
    log(LogType::RpcLog, level, file, line, "", msg, getRuntime().getCurrentRequestContext().getReqId());
}

void Logger::log(
    LogLevel level,
    const char* file,
    int line,
    const std::string& msg,
    const std::string& reqId
)
{
    log(LogType::RpcLog, level, file, line, "", msg, reqId);
}

void Logger::log(
    LogLevel level,
    const char* file,
    int line,
    const char* function,
    const std::string& msg
)
{
    log(LogType::RpcLog, level, file, line, function, msg);
}

void Logger::log(
    LogLevel level,
    const char* file,
    int line,
    const char* function,
    const std::string& msg,
    const std::string& reqId
)
{
    log(LogType::RpcLog, level, file, line, function, msg, reqId);
}

void Logger::log(
    LogType type,
    LogLevel level,
    const char* file,
    int line,
    const char* function,
    const std::string& msg
)
{
    log(type, level, file, line, function, msg, getRuntime().getCurrentRequestContext().getReqId());
}

void Logger::log(
    LogType type,
    LogLevel level,
    const char* file,
    int line,
    const char* function,
    const std::string& msg,
    const std::string& reqId
)
{
    LogEvent event = makeLogEvent(type, level, file, line, function, msg, reqId);
    getLoggerState().write(type, level, formatLogEvent(event));
}

}
