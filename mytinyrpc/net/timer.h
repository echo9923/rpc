#pragma once

#include <cstdint>
#include <functional>

namespace tinyrpc {

// 返回当前系统时间，单位毫秒。
// 当前用于 TimerEvent 计算到期时间；后续 Timer/timerfd 会复用该时间基准。
int64_t getNowMs();

// TimerEvent 表示一个内存级定时任务。
//
// 当前任务只描述定时任务本身，不创建 timerfd，也不注册到 Reactor。
// TimerEvent 的回调由后续 Timer 在到期时显式调用 run() 触发。
class TimerEvent {
 public:
    using Callback = std::function<void()>;

    TimerEvent(int64_t intervalMs, bool repeated, Callback callback);

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
    bool m_canceled {false};
    Callback m_callback;
};

}
