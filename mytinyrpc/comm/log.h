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
  static void log(LogLevel level, const char* file, int line, const std::string& msg);

 private:
  static const char* levelToString(LogLevel level);
};

}

#define DebugLog(msg) tinyrpc::Logger::log(tinyrpc::LogLevel::Debug, __FILE__, __LINE__, msg)
#define InfoLog(msg)  tinyrpc::Logger::log(tinyrpc::LogLevel::Info,  __FILE__, __LINE__, msg)
#define WarnLog(msg)  tinyrpc::Logger::log(tinyrpc::LogLevel::Warn,  __FILE__, __LINE__, msg)
#define ErrorLog(msg) tinyrpc::Logger::log(tinyrpc::LogLevel::Error, __FILE__, __LINE__, msg)
