#include "coroutine/coroutinepool.h"

#include <utility>

namespace tinyrpc {

CoroutinePool::CoroutinePool(size_t capacity, size_t stackSize)
    : m_capacity(capacity),
      m_stackSize(stackSize)
{
}

std::unique_ptr<Coroutine> CoroutinePool::getCoroutine(std::function<void()> cb)
{
    if (!m_idleCoroutines.empty()) {
        auto coroutine = std::move(m_idleCoroutines.front());
        m_idleCoroutines.pop();
        if (!coroutine->reset(std::move(cb))) {
            return nullptr;
        }
        return coroutine;
    }

    if (m_createdCount >= m_capacity) {
        return nullptr;
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

    m_idleCoroutines.push(std::move(coroutine));
    return true;
}

size_t CoroutinePool::getCapacity() const
{
    return m_capacity;
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
