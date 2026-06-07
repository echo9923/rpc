#pragma once

#include "coroutine/coroutine.h"
#include "coroutine/memory.h"

#include <cstddef>
#include <functional>
#include <memory>
#include <unordered_map>
#include <vector>

namespace tinyrpc {

class Config;

enum class CoroutinePoolExhaustPolicy {
    ReturnNull,
    ExpandBlock,
};

// CoroutinePool — 可配置扩展策略的协程对象池。
//
// 池负责复用 Coroutine 对象及其栈空间，不负责调度。
// 默认耗尽策略为返回 nullptr；配置开启后按块扩展容量再创建协程。
class CoroutinePool {
 public:
    explicit CoroutinePool(
        size_t capacity,
        size_t stackSize = 128 * 1024,
        CoroutinePoolExhaustPolicy exhaustPolicy = CoroutinePoolExhaustPolicy::ReturnNull,
        size_t expandBlockSize = 0
    );
    explicit CoroutinePool(const Config& config);

    std::unique_ptr<Coroutine> getCoroutine(std::function<void()> cb);
    bool returnCoroutine(std::unique_ptr<Coroutine> coroutine);

    size_t getCapacity() const;
    size_t getInitialCapacity() const;
    size_t getCreatedCount() const;
    size_t getIdleCount() const;
    size_t getFreeStackBlockCount() const;

 private:
    bool expandCapacity();
    bool addStackBlockPool(size_t blockCount);
    void *allocateStackBlock();
    bool deallocateStackBlock(void *stack);

    size_t m_initialCapacity {0};
    size_t m_capacity {0};
    size_t m_stackSize {0};
    size_t m_createdCount {0};
    CoroutinePoolExhaustPolicy m_exhaustPolicy {CoroutinePoolExhaustPolicy::ReturnNull};
    size_t m_expandBlockSize {0};
    std::vector<std::unique_ptr<Coroutine>> m_idleCoroutines;
    std::vector<std::unique_ptr<FixedMemoryPool>> m_stackPools;
    std::unordered_map<Coroutine *, void *> m_activeStackBlocks;
};

}  // namespace tinyrpc
