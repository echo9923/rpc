#include "comm/log.h"
#include "comm/runtime.h"

#include <condition_variable>
#include <chrono>
#include <ctime>
#include <deque>
#include <fstream>
#include <iostream>
#include <mutex>
#include <sstream>
#include <thread>

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
        m_file.open(path, std::ios::out | std::ios::app);
        if (!m_file.is_open()) {
            m_consoleMode = true;
            return false;
        }

        m_level = level;
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
        flushFileLocked();
        if (m_file.is_open()) {
            m_file.close();
        }
        m_messages.clear();
        m_pendingCount = 0;
        m_async = false;
        m_stopping = false;
        m_enabled = true;
        m_level = LogLevel::Debug;
        m_consoleMode = true;
    }

    void setLevel(LogLevel level)
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_level = level;
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
        flushFileLocked();
    }

    void write(LogLevel level, const std::string& line)
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (!m_enabled || level < m_level) {
            return;
        }

        if (m_consoleMode) {
            std::cout << line << std::endl;
            return;
        }

        if (m_async) {
            m_messages.push_back(line);
            m_condition.notify_one();
            return;
        }

        // std::ofstream::operator<< 写入文件缓冲；flush() 或 shutdown() 负责强制刷盘。
        m_file << line << std::endl;
    }

 private:
    void runWorker()
    {
        while (true) {
            std::string line;
            {
                std::unique_lock<std::mutex> lock(m_mutex);
                m_condition.wait(lock, [this]() {
                    return m_stopping || !m_messages.empty();
                });
                if (m_stopping && m_messages.empty()) {
                    m_condition.notify_all();
                    return;
                }

                line = std::move(m_messages.front());
                m_messages.pop_front();
                ++m_pendingCount;
            }

            {
                std::lock_guard<std::mutex> lock(m_mutex);
                if (m_file.is_open()) {
                    // 后台线程串行写文件，避免业务线程直接阻塞在磁盘 I/O 上。
                    m_file << line << std::endl;
                }
                --m_pendingCount;
                if (m_messages.empty() && m_pendingCount == 0) {
                    m_condition.notify_all();
                }
            }
        }
    }

    void flushFileLocked()
    {
        if (m_file.is_open()) {
            m_file.flush();
        }
    }

    std::mutex m_mutex;
    std::condition_variable m_condition;
    std::ofstream m_file;
    std::deque<std::string> m_messages;
    std::thread m_worker;
    size_t m_pendingCount {0};
    bool m_async {false};
    bool m_stopping {false};
    bool m_enabled {true};
    bool m_consoleMode {true};
    LogLevel m_level {LogLevel::Debug};
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

std::string formatLine(
    LogLevel level,
    const char* file,
    int line,
    const std::string& msg,
    const std::string& reqId
)
{
    std::ostringstream stream;
    stream << "[" << currentTimeString() << "] "
           << "[" << logLevelToString(level) << "] "
           << "[tid=" << formatThreadId() << "] "
           << "[" << file << ":" << line << "] ";
    if (!reqId.empty()) {
        stream << "[reqId=" << reqId << "] ";
    }
    stream << msg;
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

void Logger::shutdown()
{
    getLoggerState().shutdown();
}

void Logger::setLevel(LogLevel level)
{
    getLoggerState().setLevel(level);
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
    log(level, file, line, msg, getRuntime().getCurrentRequestContext().getReqId());
}

void Logger::log(
    LogLevel level,
    const char* file,
    int line,
    const std::string& msg,
    const std::string& reqId
)
{
    getLoggerState().write(level, formatLine(level, file, line, msg, reqId));
}

}
