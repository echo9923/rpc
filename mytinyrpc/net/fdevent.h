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
     int m_fd {-1};                           // 文件描述符，-1 表示无效
     Reactor *m_reactor {nullptr};             // 所属的 Reactor（事件循环），用于注册/更新/删除事件
     Coroutine *m_coroutine {nullptr};         // 挂载在此 FdEvent 上的协程（非拥有），用于 IO hook 等待时挂起/恢复
     uint32_t m_coroutineListenEvent {0};      // 协程等待的事件类型（EPOLLIN / EPOLLOUT），Reactor 据此判断是否恢复协程
     uint32_t m_listenEvents {0};              // 当前注册到 epoll 的监听事件集合（EPOLLIN | EPOLLOUT 等）
     bool m_isRegistered {false};              // 标记是否已注册到 Reactor 的 epoll 实例中

     std::function<void()> m_readCallback;     // 可读事件触发时的回调函数
     std::function<void()> m_writeCallback;    // 可写事件触发时的回调函数
};

}
