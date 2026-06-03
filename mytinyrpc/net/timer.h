#pragma once

#include "net/fdevent.h"

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <vector>

namespace tinyrpc {

class Reactor;

// 返回当前系统时间，单位毫秒。
// 当前用于 TimerTask 计算到期时间；后续 Timer/timerfd 会复用该时间基准。
int64_t getNowMs();

// TimerTask 表示一个内存级定时任务。
//
// 当前任务只描述定时任务本身，不创建 timerfd，也不注册到 Reactor。
// TimerTask 的回调由后续 Timer 在到期时显式调用 run() 触发。
class TimerTask {
 public:
    using Callback = std::function<void()>;

   TimerTask(int64_t intervalMs, bool repeated, Callback callback);
   TimerTask(const TimerTask&) = delete;
   TimerTask& operator=(const TimerTask&) = delete;

    int64_t getIntervalMs() const;
    int64_t getExpireTimeMs() const;
    bool isRepeated() const;
    bool isCanceled() const;

    bool isExpired(int64_t nowMs) const;
    void cancel();
    void resetTime();
    void resetTime(int64_t intervalMs);
    void run();

 private:
    int64_t normalizeInterval(int64_t intervalMs) const;

    int64_t m_intervalMs {0};
    int64_t m_expireTimeMs {0};
    bool m_repeated {false};
    std::atomic<bool> m_canceled {false};
    Callback m_callback;
};

// Timer 使用 Linux timerfd 将时间到期转换成 fd 可读事件。
// Reactor 只需要像处理普通 fd 一样处理 Timer 的 FdEvent。
class Timer {
 public:
    explicit Timer(Reactor *reactor);
    ~Timer();

    Timer(const Timer&) = delete;
    Timer& operator=(const Timer&) = delete;

    int getFd() const;
   bool addTimerTask(const std::shared_ptr<TimerTask>& task);
   bool delTimerTask(const std::shared_ptr<TimerTask>& task);
   std::size_t getPendingTaskCount() const;

 private:
    void handleTimerReadable();
    void resetTimerFd();
   void removeCanceledTasks();

    int m_timerFd {-1};
    FdEvent m_fdEvent;
   std::vector<std::shared_ptr<TimerTask>> m_tasks;
};

}
