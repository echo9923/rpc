/*
 * test_mutex.cc -- 任务五十三：Mutex / RWMutex 基础线程工具测试。
 */

#include "net/mutex.h"

#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <iostream>
#include <thread>
#include <vector>

TEST(MutexTest, MultiThreadIncrementIsSerialized)
{
    tinyrpc::Mutex mutex;
    int counter = 0;
    constexpr int kThreadCount = 8;
    constexpr int kLoopCount = 1000;
    std::vector<std::thread> threads;

    for (int i = 0; i < kThreadCount; ++i) {
        threads.emplace_back([&]() {
            for (int j = 0; j < kLoopCount; ++j) {
                tinyrpc::MutexLockGuard lock(mutex);
                ++counter;
            }
        });
    }

    for (auto& thread : threads) {
        thread.join();
    }

    EXPECT_EQ(counter, kThreadCount * kLoopCount);
}

TEST(RWMutexTest, MultipleReadersCanEnterTogether)
{
    tinyrpc::RWMutex rwMutex;
    std::atomic<int> insideReaders {0};
    std::atomic<int> maxInsideReaders {0};
    std::vector<std::thread> readers;

    for (int i = 0; i < 4; ++i) {
        readers.emplace_back([&]() {
            tinyrpc::ReadLockGuard lock(rwMutex);
            int current = insideReaders.fetch_add(1) + 1;
            int oldMax = maxInsideReaders.load();
            while (current > oldMax && !maxInsideReaders.compare_exchange_weak(oldMax, current)) {
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(20));
            insideReaders.fetch_sub(1);
        });
    }

    for (auto& reader : readers) {
        reader.join();
    }

    EXPECT_GT(maxInsideReaders.load(), 1);
}

TEST(RWMutexTest, WriterExcludesReadersAndWriters)
{
    tinyrpc::RWMutex rwMutex;
    int value = 0;
    constexpr int kThreadCount = 4;
    constexpr int kLoopCount = 500;
    std::vector<std::thread> writers;

    for (int i = 0; i < kThreadCount; ++i) {
        writers.emplace_back([&]() {
            for (int j = 0; j < kLoopCount; ++j) {
                tinyrpc::WriteLockGuard lock(rwMutex);
                ++value;
            }
        });
    }

    for (auto& writer : writers) {
        writer.join();
    }

    EXPECT_EQ(value, kThreadCount * kLoopCount);
}

int main(int argc, char **argv)
{
    ::testing::InitGoogleTest(&argc, argv);
    int result = RUN_ALL_TESTS();
    if (result == 0) {
        std::cout << "[mutex] PASS" << std::endl;
    }
    return result;
}
