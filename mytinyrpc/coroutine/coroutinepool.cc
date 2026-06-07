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
        if (!coroutine->reset(std::move(cb))) {
            return nullptr;
        }
        return coroutine;
    }

    if (m_createdCount >= m_capacity) {
        if (!expandCapacity()) {
            return nullptr;
        }
    }

    ++m_createdCount;
    return std::make_unique<Coroutine>(std::move(cb), m_stackSize);
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

    if (!coroutine->reset([]() {})) {
        return false;
    }

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

    m_capacity += m_expandBlockSize;
    return true;
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

}  // namespace tinyrpc
