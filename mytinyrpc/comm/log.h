#pragma once

#include <string>

namespace tinyrpc {

enum class LogLevel {
  DEBUG = 1,
  INFO = 2,
  WARN = 3,
  ERROR = 4
};

class Logger {
 public:
  static void log(LogLevel level, const char* file, int line, const std::string& msg);

 private:
  static const char* levelToString(LogLevel level);
};

}

#define DebugLog(msg) tinyrpc::Logger::log(tinyrpc::LogLevel::DEBUG, __FILE__, __LINE__, msg)
#define InfoLog(msg)  tinyrpc::Logger::log(tinyrpc::LogLevel::INFO,  __FILE__, __LINE__, msg)
#define WarnLog(msg)  tinyrpc::Logger::log(tinyrpc::LogLevel::WARN,  __FILE__, __LINE__, msg)
#define ErrorLog(msg) tinyrpc::Logger::log(tinyrpc::LogLevel::ERROR, __FILE__, __LINE__, msg)
