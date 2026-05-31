#pragma once

#include "net/fdevent.h"

#include <cstdint>
#include <memory>
#include <unordered_map>

namespace tinyrpc {

class Timer;

class Reactor {
 public:
  Reactor();
  ~Reactor();

  int getEpollFd() const;
  Timer* getTimer() const;

  bool epollAdd(FdEvent* event);
  bool epollMod(FdEvent* event);
  bool epollDel(FdEvent* event);

  int waitOnce(int timeoutMs);

 private:
  static constexpr int kMaxEvents = 64;

  int m_epollFd {-1};
  std::unique_ptr<Timer> m_timer;

  // fd → FdEvent* 映射，epollAdd/epollDel 时维护。
  // 用于快速查找和防止重复注册。
  std::unordered_map<int, FdEvent*> m_events;
};

}
