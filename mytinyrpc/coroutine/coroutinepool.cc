#include "coroutine/coroutinepool.h"

#include "comm/config.h"

#include <limits>
#include <utility>

namespace tinyrpc {

CoroutinePool::CoroutinePool(
    size_t capacity,
    size_t stackSize,
    CoroutinePoolExhaustPolicy exhaustPolicy,
    size_t expandBlockSize
)
    : m_initialCapacity(capacity),
      m_capacity(capacity),
      m_stackSize(stackSize),
      m_exhaustPolicy(exhaustPolicy),
      m_expandBlockSize(expandBlockSize)
{
    if (m_expandBlockSize == 0 && m_exhaustPolicy == CoroutinePoolExhaustPolicy::ExpandBlock) {
        m_expandBlockSize = (m_initialCapacity == 0) ? 1 : m_initialCapacity;
    }
    addStackBlockPool(m_initialCapacity);
}

CoroutinePool::CoroutinePool(const Config& config)
    : CoroutinePool(
        static_cast<size_t>(config.getCoroutinePoolSize()),
        static_cast<size_t>(config.getCoroutineStackSizeBytes()),
        config.isCoroutinePoolExpandOnExhausted()
            ? CoroutinePoolExhaustPolicy::ExpandBlock
            : CoroutinePoolExhaustPolicy::ReturnNull
    )
{
}

std::unique_ptr<Coroutine> CoroutinePool::getCoroutine(std::function<void()> cb)
{
    if (!m_idleCoroutines.empty()) {
        auto coroutine = std::move(m_idleCoroutines.back());
        m_idleCoroutines.pop_back();
        void *stack = allocateStackBlock();
        if (stack == nullptr) {
            m_idleCoroutines.push_back(std::move(coroutine));
            return nullptr;
        }
        if (!coroutine->resetWithExternalStack(std::move(cb), stack, m_stackSize)) {
            deallocateStackBlock(stack);
            m_idleCoroutines.push_back(std::move(coroutine));
            return nullptr;
        }
        m_activeStackBlocks[coroutine.get()] = stack;
        return coroutine;
    }

    if (m_createdCount >= m_capacity) {
        if (!expandCapacity()) {
            return nullptr;
        }
    }

    void *stack = allocateStackBlock();
    if (stack == nullptr) {
        return nullptr;
    }

    ++m_createdCount;
    auto coroutine = std::make_unique<Coroutine>(std::move(cb), stack, m_stackSize);
    m_activeStackBlocks[coroutine.get()] = stack;
    return coroutine;
}

bool CoroutinePool::returnCoroutine(std::unique_ptr<Coroutine> coroutine)
{
    if (coroutine == nullptr) {
        return false;
    }

    if (m_idleCoroutines.size() >= m_capacity) {
        return false;
    }

    CoroutineState state = coroutine->getState();
    if (state != CoroutineState::Finished && state != CoroutineState::Ready) {
        return false;
    }

    auto iter = m_activeStackBlocks.find(coroutine.get());
    if (iter == m_activeStackBlocks.end()) {
        return false;
    }

    void *stack = iter->second;
    void *detachedStack = coroutine->detachExternalStack();
    if (detachedStack != stack) {
        return false;
    }
    if (!deallocateStackBlock(stack)) {
        return false;
    }
    m_activeStackBlocks.erase(iter);

    m_idleCoroutines.push_back(std::move(coroutine));
    return true;
}

bool CoroutinePool::expandCapacity()
{
    if (m_exhaustPolicy != CoroutinePoolExhaustPolicy::ExpandBlock || m_expandBlockSize == 0) {
        return false;
    }
    if (m_capacity > std::numeric_limits<size_t>::max() - m_expandBlockSize) {
        return false;
    }
    if (!addStackBlockPool(m_expandBlockSize)) {
        return false;
    }

    m_capacity += m_expandBlockSize;
    return true;
}

bool CoroutinePool::addStackBlockPool(size_t blockCount)
{
    if (blockCount == 0) {
        return true;
    }
    if (m_stackSize == 0) {
        return false;
    }

    m_stackPools.push_back(std::make_unique<FixedMemoryPool>(m_stackSize, blockCount));
    return true;
}

void *CoroutinePool::allocateStackBlock()
{
    for (auto& pool : m_stackPools) {
        void *stack = pool->allocate();
        if (stack != nullptr) {
            return stack;
        }
    }
    return nullptr;
}

bool CoroutinePool::deallocateStackBlock(void *stack)
{
    for (auto& pool : m_stackPools) {
        if (pool->owns(stack)) {
            return pool->deallocate(stack);
        }
    }
    return false;
}

size_t CoroutinePool::getCapacity() const
{
    return m_capacity;
}

size_t CoroutinePool::getInitialCapacity() const
{
    return m_initialCapacity;
}

size_t CoroutinePool::getCreatedCount() const
{
    return m_createdCount;
}

size_t CoroutinePool::getIdleCount() const
{
    return m_idleCoroutines.size();
}

size_t CoroutinePool::getFreeStackBlockCount() const
{
    size_t freeCount = 0;
    for (const auto& pool : m_stackPools) {
        freeCount += pool->getFreeCount();
    }
    return freeCount;
}

}  // namespace tinyrpc
