#pragma once

#include <cstddef>
#include <memory>
#include <unordered_set>
#include <vector>

namespace tinyrpc {

// FixedMemoryPool — 固定大小内存块池。
//
// 构造时一次性分配 blockCount 个 block，每个 block 大小为 blockSize。
// allocate() 借出一个空闲 block；deallocate() 只允许归还本池分配的 block。
class FixedMemoryPool {
 public:
    FixedMemoryPool(size_t blockSize, size_t blockCount);
    ~FixedMemoryPool() = default;

    FixedMemoryPool(const FixedMemoryPool&) = delete;
    FixedMemoryPool& operator=(const FixedMemoryPool&) = delete;

    void *allocate();
    bool deallocate(void *ptr);

    size_t getBlockSize() const;
    size_t getCapacity() const;
    size_t getFreeCount() const;
    bool owns(void *ptr) const;

 private:
    size_t m_blockSize {0};
    size_t m_capacity {0};
    std::vector<std::unique_ptr<char[]>> m_blocks;
    std::vector<void *> m_freeBlocks;
    std::unordered_set<void *> m_allBlocks;
    std::unordered_set<void *> m_freeSet;
};

}  // namespace tinyrpc
