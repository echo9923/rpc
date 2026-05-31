#pragma once

#include "net/fdevent.h"

#include <atomic>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <queue>
#include <unordered_map>

namespace tinyrpc {

class Timer;

class Reactor {
 public:
  Reactor();
  ~Reactor();

  int getEpollFd() const;
  Timer* getTimer() const;

  void addTask(std::function<void()> task);
  void loop();
  void stop();

  bool addFdEvent(FdEvent* event);
  bool delFdEvent(FdEvent* event);

  bool epollAdd(FdEvent* event);
  bool epollMod(FdEvent* event);
  bool epollDel(FdEvent* event);

  int waitOnce(int timeoutMs);

 private:
  static constexpr int kMaxEvents = 64;

  int m_epollFd {-1};
  std::unique_ptr<Timer> m_timer;
  int m_wakeupFd {-1};
  FdEvent m_wakeupEvent;
  std::atomic<bool> m_stop {false};
  std::mutex m_taskMutex;
  std::queue<std::function<void()>> m_pendingTasks;

  // fd → FdEvent* 映射，epollAdd/epollDel 时维护。
  // 用于快速查找和防止重复注册。
  std::unordered_map<int, FdEvent*> m_events;

  bool initWakeupFd();
  void wakeup();
  void handleWakeup();
  void runPendingTasks();
};

}
