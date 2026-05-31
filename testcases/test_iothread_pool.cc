/*
 * test_iothread_pool.cc -- 任务五十五：IOThreadPool 测试。
 */

#include "net/iothread_pool.h"

#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <iostream>
#include <mutex>
#include <thread>
#include <unordered_set>
#include <vector>

namespace {

void waitUntil(const std::function<bool()>& done)
{
    for (int i = 0; i < 100 && !done(); ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
}

}

TEST(IOThreadPoolTest, NextThreadReturnsRoundRobinThreads)
{
    tinyrpc::IOThreadPool pool(2);

    auto *first = pool.getNextIOThread();
    auto *second = pool.getNextIOThread();
    auto *third = pool.getNextIOThread();

    ASSERT_NE(first, nullptr);
    ASSERT_NE(second, nullptr);
    ASSERT_NE(third, nullptr);
    EXPECT_NE(first, second);
    EXPECT_EQ(first, third);

    pool.stop();
}

TEST(IOThreadPoolTest, BroadcastRunsOnceOnEachThread)
{
    tinyrpc::IOThreadPool pool(3);
    std::atomic<int> runCount {0};
    std::vector<std::thread::id> threadIds;
    std::mutex idsMutex;

    pool.broadcastTask([&]() {
        {
            std::lock_guard<std::mutex> lock(idsMutex);
            threadIds.push_back(std::this_thread::get_id());
        }
        runCount.fetch_add(1);
    });

    waitUntil([&]() {
        return runCount.load() == 3;
    });

    std::unordered_set<std::thread::id> uniqueIds(threadIds.begin(), threadIds.end());
    EXPECT_EQ(runCount.load(), 3);
    EXPECT_EQ(uniqueIds.size(), 3u);

    pool.stop();
}

TEST(IOThreadPoolTest, AddTaskByIndexRunsOnlyOnTargetThread)
{
    tinyrpc::IOThreadPool pool(2);
    std::atomic<bool> done {false};
    std::thread::id callbackThreadId;
    auto *target = pool.getIOThreadByIndex(1);
    ASSERT_NE(target, nullptr);
    std::thread::id targetThreadId = target->getThreadId();

    ASSERT_TRUE(pool.addTaskByIndex(1, [&]() {
        callbackThreadId = std::this_thread::get_id();
        done.store(true);
    }));

    waitUntil([&]() {
        return done.load();
    });

    EXPECT_TRUE(done.load());
    EXPECT_EQ(callbackThreadId, targetThreadId);
    EXPECT_FALSE(pool.addTaskByIndex(8, []() {}));

    pool.stop();
}

int main(int argc, char **argv)
{
    ::testing::InitGoogleTest(&argc, argv);
    int result = RUN_ALL_TESTS();
    if (result == 0) {
        std::cout << "[iothread_pool] PASS" << std::endl;
    }
    return result;
}
