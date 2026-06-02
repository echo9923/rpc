#include "coroutine/memory.h"

namespace tinyrpc {

FixedMemoryPool::FixedMemoryPool(size_t blockSize, size_t blockCount)
    : m_blockSize(blockSize),
      m_capacity(blockCount)
{
    m_blocks.reserve(blockCount);
    m_freeBlocks.reserve(blockCount);
    for (size_t i = 0; i < blockCount; ++i) {
        auto block = std::make_unique<char[]>(blockSize);
        void *ptr = block.get();
        m_allBlocks.insert(ptr);
        m_freeSet.insert(ptr);
        m_freeBlocks.push_back(ptr);
        m_blocks.push_back(std::move(block));
    }
}

void *FixedMemoryPool::allocate()
{
    if (m_freeBlocks.empty()) {
        return nullptr;
    }

    void *ptr = m_freeBlocks.back();
    m_freeBlocks.pop_back();
    m_freeSet.erase(ptr);
    return ptr;
}

bool FixedMemoryPool::deallocate(void *ptr)
{
    if (ptr == nullptr) {
        return false;
    }
    if (!owns(ptr)) {
        return false;
    }
    if (m_freeSet.find(ptr) != m_freeSet.end()) {
        return false;
    }

    m_freeSet.insert(ptr);
    m_freeBlocks.push_back(ptr);
    return true;
}

size_t FixedMemoryPool::getBlockSize() const
{
    return m_blockSize;
}

size_t FixedMemoryPool::getCapacity() const
{
    return m_capacity;
}

size_t FixedMemoryPool::getFreeCount() const
{
    return m_freeBlocks.size();
}

bool FixedMemoryPool::owns(void *ptr) const
{
    return m_allBlocks.find(ptr) != m_allBlocks.end();
}

}  // namespace tinyrpc
