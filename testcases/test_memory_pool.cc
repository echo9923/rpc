/*
 * test_memory_pool.cc -- 任务七十三：固定块内存池测试。
 */

#include "coroutine/memory.h"

#include <gtest/gtest.h>

#include <cstring>
#include <iostream>

TEST(MemoryPoolTest, AllocateUntilExhausted)
{
    tinyrpc::FixedMemoryPool pool(128, 2);

    void *first = pool.allocate();
    void *second = pool.allocate();
    void *third = pool.allocate();

    EXPECT_NE(first, nullptr);
    EXPECT_NE(second, nullptr);
    EXPECT_NE(first, second);
    EXPECT_EQ(third, nullptr);
    EXPECT_EQ(pool.getFreeCount(), 0u);
    EXPECT_EQ(pool.getCapacity(), 2u);
    EXPECT_EQ(pool.getBlockSize(), 128u);
}

TEST(MemoryPoolTest, DeallocateThenAllocateReusesBlock)
{
    tinyrpc::FixedMemoryPool pool(64, 1);

    void *first = pool.allocate();
    ASSERT_NE(first, nullptr);
    ASSERT_TRUE(pool.deallocate(first));

    void *second = pool.allocate();

    EXPECT_EQ(second, first);
    EXPECT_EQ(pool.getFreeCount(), 0u);
}

TEST(MemoryPoolTest, RejectsForeignNullAndDoubleFree)
{
    tinyrpc::FixedMemoryPool pool(64, 1);

    int external = 0;
    void *ptr = pool.allocate();
    ASSERT_NE(ptr, nullptr);

    EXPECT_FALSE(pool.deallocate(nullptr));
    EXPECT_FALSE(pool.deallocate(&external));
    EXPECT_TRUE(pool.deallocate(ptr));
    EXPECT_FALSE(pool.deallocate(ptr));
    EXPECT_EQ(pool.getFreeCount(), 1u);
}

TEST(MemoryPoolTest, OwnsOnlyPoolBlocks)
{
    tinyrpc::FixedMemoryPool pool(64, 2);
    int external = 0;

    void *first = pool.allocate();
    void *second = pool.allocate();

    EXPECT_TRUE(pool.owns(first));
    EXPECT_TRUE(pool.owns(second));
    EXPECT_FALSE(pool.owns(&external));
}

TEST(MemoryPoolTest, AllocatedBlockCanStoreBytes)
{
    tinyrpc::FixedMemoryPool pool(256, 1);
    void *ptr = pool.allocate();
    ASSERT_NE(ptr, nullptr);

    std::memset(ptr, 0x5A, 256);
    auto *bytes = static_cast<unsigned char *>(ptr);

    EXPECT_EQ(bytes[0], 0x5A);
    EXPECT_EQ(bytes[255], 0x5A);
    EXPECT_TRUE(pool.deallocate(ptr));
}

int main(int argc, char **argv)
{
    ::testing::InitGoogleTest(&argc, argv);
    int result = RUN_ALL_TESTS();
    if (result == 0) {
        std::cout << "[memory_pool] PASS" << std::endl;
    }
    return result;
}
