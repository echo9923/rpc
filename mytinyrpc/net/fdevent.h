#pragma once

#include <cstdint>
#include <functional>

namespace tinyrpc {

class Coroutine;
class Reactor;

class FdEvent {
 public:
  explicit FdEvent(int fd = -1);

  void setFd(int fd);
  int getFd() const;

  void setReactor(Reactor *reactor);
  Reactor *getReactor() const;

  // 协程挂载点：用于后续 IO hook 将等待 IO 的协程挂到此 FdEvent 上。
  // FdEvent 只保存非拥有的 Coroutine*，不负责创建、恢复或销毁。
  void setCoroutine(Coroutine *coroutine);
  Coroutine *getCoroutine() const;
  void clearCoroutine();

  // 协程等待的事件类型（EPOLLIN 或 EPOLLOUT），用于 Reactor 判断
  // 是否应该在当前触发事件上恢复协程，避免在错误事件上恢复。
  void setCoroutineListenEvent(uint32_t event);
  uint32_t getCoroutineListenEvent() const;

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
  Coroutine *m_coroutine {nullptr};
  uint32_t m_coroutineListenEvent {0};
  uint32_t m_listenEvents {0};
  bool m_isRegistered {false};

  std::function<void()> m_readCallback;
  std::function<void()> m_writeCallback;
};

}
