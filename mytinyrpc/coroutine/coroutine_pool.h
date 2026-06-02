#pragma once

#include "coroutine/coroutine.h"

#include <cstddef>
#include <functional>
#include <memory>
#include <queue>

namespace tinyrpc {

// CoroutinePool — 固定容量协程对象池。
//
// 池负责复用 Coroutine 对象及其栈空间，不负责调度。
// 当空闲池为空且已创建数量达到容量上限时，getCoroutine() 返回 nullptr。
class CoroutinePool {
 public:
    explicit CoroutinePool(size_t capacity, size_t stackSize = 128 * 1024);

    std::unique_ptr<Coroutine> getCoroutine(std::function<void()> cb);
    bool returnCoroutine(std::unique_ptr<Coroutine> coroutine);

    size_t getCapacity() const;
    size_t getCreatedCount() const;
    size_t getIdleCount() const;

 private:
    size_t m_capacity {0};
    size_t m_stackSize {0};
    size_t m_createdCount {0};
    std::queue<std::unique_ptr<Coroutine>> m_idleCoroutines;
};

}  // namespace tinyrpc
