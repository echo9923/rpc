#include "net/timer.h"

#include <chrono>
#include <utility>

namespace tinyrpc {

int64_t getNowMs()
{
    auto now = std::chrono::steady_clock::now();
    auto duration = now.time_since_epoch();
    return std::chrono::duration_cast<std::chrono::milliseconds>(duration).count();
}

TimerEvent::TimerEvent(int64_t intervalMs, bool repeated, Callback callback)
    : m_intervalMs(normalizeInterval(intervalMs)),
      m_repeated(repeated),
      m_callback(std::move(callback))
{
    resetTime(m_intervalMs);
}

int64_t TimerEvent::getIntervalMs() const
{
    return m_intervalMs;
}

int64_t TimerEvent::getExpireTimeMs() const
{
    return m_expireTimeMs;
}

bool TimerEvent::isRepeated() const
{
    return m_repeated;
}

bool TimerEvent::isCanceled() const
{
    return m_canceled;
}

bool TimerEvent::isExpired(int64_t nowMs) const
{
    return !m_canceled && nowMs >= m_expireTimeMs;
}

void TimerEvent::cancel()
{
    m_canceled = true;
}

void TimerEvent::resetTime()
{
    resetTime(m_intervalMs);
}

void TimerEvent::resetTime(int64_t intervalMs)
{
    m_intervalMs = normalizeInterval(intervalMs);
    m_expireTimeMs = getNowMs() + m_intervalMs;
    m_canceled = false;
}

void TimerEvent::run()
{
    if (m_canceled) {
        return;
    }

    if (m_callback) {
        m_callback();
    }

    if (m_repeated) {
        resetTime();
        return;
    }

    cancel();
}

int64_t TimerEvent::normalizeInterval(int64_t intervalMs) const
{
    if (intervalMs < 0) {
        return 0;
    }
    return intervalMs;
}

}
