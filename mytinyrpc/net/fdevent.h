#pragma once

#include <cstdint>
#include <functional>

namespace tinyrpc {

class FdEvent {
 public:
  explicit FdEvent(int fd = -1);

  void setFd(int fd);
  int getFd() const;

  void addListenEvent(uint32_t event);
  void delListenEvent(uint32_t event);
  uint32_t getListenEvents() const;

  void setReadCallback(std::function<void()> cb);
  void setWriteCallback(std::function<void()> cb);

  void handleEvent(uint32_t triggerEvents);

 private:
  int m_fd {-1};
  uint32_t m_listenEvents {0};

  std::function<void()> m_readCallback;
  std::function<void()> m_writeCallback;
};

}
