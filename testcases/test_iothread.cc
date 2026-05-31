/*
 * test_iothread.cc -- 任务五十四：IOThread 生命周期测试。
 */

#include "net/iothread.h"

#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <iostream>
#include <thread>

TEST(IOThreadTest, ConstructorStartsReactorThread)
{
    tinyrpc::IOThread ioThread;

    ASSERT_NE(ioThread.getReactor(), nullptr);
    EXPECT_TRUE(ioThread.isStarted());
    EXPECT_NE(ioThread.getThreadId(), std::thread::id());

    ioThread.stop();
    EXPECT_FALSE(ioThread.isStarted());
}

TEST(IOThreadTest, AddTaskRunsOnIOThread)
{
    tinyrpc::IOThread ioThread;
    std::atomic<bool> done {false};
    std::thread::id callbackThreadId;

    ioThread.addTask([&]() {
        callbackThreadId = std::this_thread::get_id();
        done.store(true);
    });

    for (int i = 0; i < 50 && !done.load(); ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    EXPECT_TRUE(done.load());
    EXPECT_EQ(callbackThreadId, ioThread.getThreadId());

    ioThread.stop();
}

TEST(IOThreadTest, StopExitsThreadAndIsIdempotent)
{
    tinyrpc::IOThread ioThread;
    ASSERT_TRUE(ioThread.isStarted());

    ioThread.stop();
    ioThread.stop();

    EXPECT_FALSE(ioThread.isStarted());
}

int main(int argc, char **argv)
{
    ::testing::InitGoogleTest(&argc, argv);
    int result = RUN_ALL_TESTS();
    if (result == 0) {
        std::cout << "[iothread] PASS" << std::endl;
    }
    return result;
}
