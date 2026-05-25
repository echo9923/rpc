#pragma once

#include <cstdint>
#include <functional>

namespace tinyrpc {

class Reactor;

class FdEvent {
 public:
  explicit FdEvent(int fd = -1);

  void setFd(int fd);
  int getFd() const;

  void setReactor(Reactor *reactor);
  Reactor *getReactor() const;

  void addListenEvent(uint32_t event);
  void delListenEvent(uint32_t event);
  uint32_t getListenEvents() const;

  bool registerToReactor();
  bool updateToReactor();
  bool unregisterFromReactor();
  bool isRegistered() const;

  void setReadCallback(std::function<void()> cb);
  void setWriteCallback(std::function<void()> cb);

  void handleEvent(uint32_t triggerEvents);

 private:
  int m_fd {-1};
  Reactor *m_reactor {nullptr};
  uint32_t m_listenEvents {0};
  bool m_isRegistered {false};

  std::function<void()> m_readCallback;
  std::function<void()> m_writeCallback;
};

}
