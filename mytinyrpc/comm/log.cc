#include "comm/log.h"

#include <chrono>
#include <ctime>
#include <iostream>

namespace tinyrpc {

const char* Logger::levelToString(LogLevel level)
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

void Logger::log(LogLevel level, const char* file, int line, const std::string& msg)
{
  auto now = std::chrono::system_clock::now();
  auto tt = std::chrono::system_clock::to_time_t(now);
  auto tm = std::localtime(&tt);

  char timebuf[32];
  strftime(timebuf, sizeof(timebuf), "%Y-%m-%d %H:%M:%S", tm);

  std::cout << "[" << timebuf << "] "
            << "[" << levelToString(level) << "] "
            << "[" << file << ":" << line << "] "
            << msg << std::endl;
}

}
