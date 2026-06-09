#include "comm/thread_pool.h"

#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <iostream>
#include <mutex>
#include <thread>
#include <unordered_set>
#include <vector>

namespace {

bool waitUntil(const std::function<bool()>& done)
{
    for (int i = 0; i < 200; ++i) {
        if (done()) {
            return true;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    return done();
}

}  // namespace

TEST(ThreadPoolTest, RunsTasksOnFixedWorkerThreads)
{
    tinyrpc::ThreadPool pool(3);
    ASSERT_TRUE(pool.start());

    std::atomic<int> runCount {0};
    std::mutex idsMutex;
    std::vector<std::thread::id> threadIds;

    for (int i = 0; i < 12; ++i) {
        ASSERT_TRUE(pool.addTask([&]() {
            {
                std::lock_guard<std::mutex> lock(idsMutex);
                threadIds.push_back(std::this_thread::get_id());
            }
            runCount.fetch_add(1);
        }));
    }

    ASSERT_TRUE(waitUntil([&]() {
        return runCount.load() == 12;
    }));

    std::unordered_set<std::thread::id> uniqueIds(threadIds.begin(), threadIds.end());
    EXPECT_EQ(runCount.load(), 12);
    EXPECT_LE(uniqueIds.size(), 3u);
    EXPECT_GT(uniqueIds.size(), 0u);

    pool.stop();
}

TEST(ThreadPoolTest, StopRejectsNewTasks)
{
    tinyrpc::ThreadPool pool(2);
    ASSERT_TRUE(pool.start());
    EXPECT_TRUE(pool.isRunning());

    std::atomic<int> runCount {0};
    ASSERT_TRUE(pool.addTask([&]() {
        runCount.fetch_add(1);
    }));
    ASSERT_TRUE(waitUntil([&]() {
        return runCount.load() == 1;
    }));

    pool.stop();
    EXPECT_FALSE(pool.isRunning());
    EXPECT_FALSE(pool.addTask([]() {}));
    EXPECT_FALSE(pool.start());
}

TEST(ThreadPoolTest, StopDrainsQueuedTasksBeforeJoin)
{
    tinyrpc::ThreadPool pool(1);
    ASSERT_TRUE(pool.start());

    std::atomic<int> runCount {0};
    for (int i = 0; i < 5; ++i) {
        ASSERT_TRUE(pool.addTask([&]() {
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
            runCount.fetch_add(1);
        }));
    }

    pool.stop();
    EXPECT_EQ(runCount.load(), 5);
    EXPECT_FALSE(pool.addTask([]() {}));
}

TEST(ThreadPoolTest, DestructorJoinsWorkerThreads)
{
    std::atomic<int> runCount {0};
    {
        tinyrpc::ThreadPool pool(2);
        ASSERT_TRUE(pool.start());
        for (int i = 0; i < 4; ++i) {
            ASSERT_TRUE(pool.addTask([&]() {
                std::this_thread::sleep_for(std::chrono::milliseconds(5));
                runCount.fetch_add(1);
            }));
        }
    }

    EXPECT_EQ(runCount.load(), 4);
}

TEST(ThreadPoolTest, InvalidPoolDoesNotStart)
{
    tinyrpc::ThreadPool pool(0);

    EXPECT_FALSE(pool.start());
    EXPECT_FALSE(pool.addTask([]() {}));
    EXPECT_EQ(pool.getThreadNum(), 0u);
}

int main(int argc, char **argv)
{
    ::testing::InitGoogleTest(&argc, argv);
    int result = RUN_ALL_TESTS();
    if (result == 0) {
        std::cout << "[thread_pool] PASS" << std::endl;
    }
    return result;
}
