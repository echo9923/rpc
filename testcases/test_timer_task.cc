/*
 * test_timer_task.cc -- 任务四十七：TimerTask 与基础时间函数测试。
 */

#include "net/timer.h"

#include <gtest/gtest.h>

#include <chrono>
#include <cstdint>
#include <iostream>
#include <thread>

TEST(TimeUtilTest, GetNowMsReturnsIncreasingMillisecondTime)
{
    int64_t before = tinyrpc::getNowMs();
    std::this_thread::sleep_for(std::chrono::milliseconds(2));
    int64_t after = tinyrpc::getNowMs();

    EXPECT_GT(before, 0);
    EXPECT_GE(after, before);
}

TEST(TimerTaskTest, OneShotExpireTimeIsBasedOnCurrentTime)
{
    int64_t before = tinyrpc::getNowMs();
    tinyrpc::TimerTask task(50, false, []() {});

    EXPECT_FALSE(task.isRepeated());
    EXPECT_FALSE(task.isCanceled());
    EXPECT_EQ(task.getIntervalMs(), 50);
    EXPECT_GE(task.getExpireTimeMs(), before);
    EXPECT_FALSE(task.isExpired(before));
    EXPECT_TRUE(task.isExpired(task.getExpireTimeMs()));
}

TEST(TimerTaskTest, ResetTimeRefreshesExpireTimeAndInterval)
{
    tinyrpc::TimerTask task(10, false, []() {});
    int64_t oldExpireTime = task.getExpireTimeMs();

    std::this_thread::sleep_for(std::chrono::milliseconds(2));
    task.resetTime(80);

    EXPECT_EQ(task.getIntervalMs(), 80);
    EXPECT_GT(task.getExpireTimeMs(), oldExpireTime);
    EXPECT_FALSE(task.isCanceled());
}

TEST(TimerTaskTest, CancelPreventsCallbackExecution)
{
    int runCount = 0;
    tinyrpc::TimerTask task(0, false, [&runCount]() {
        ++runCount;
    });

    task.cancel();
    task.run();

    EXPECT_TRUE(task.isCanceled());
    EXPECT_EQ(runCount, 0);
}

TEST(TimerTaskTest, OneShotRunExecutesOnceAndCancels)
{
    int runCount = 0;
    tinyrpc::TimerTask task(0, false, [&runCount]() {
        ++runCount;
    });

    task.run();
    task.run();

    EXPECT_TRUE(task.isCanceled());
    EXPECT_EQ(runCount, 1);
}

TEST(TimerTaskTest, RepeatedRunResetsNextExpireTime)
{
    int runCount = 0;
    tinyrpc::TimerTask task(10, true, [&runCount]() {
        ++runCount;
    });
    int64_t firstExpireTime = task.getExpireTimeMs();

    std::this_thread::sleep_for(std::chrono::milliseconds(2));
    task.run();

    EXPECT_FALSE(task.isCanceled());
    EXPECT_TRUE(task.isRepeated());
    EXPECT_EQ(runCount, 1);
    EXPECT_GT(task.getExpireTimeMs(), firstExpireTime);

    task.run();

    EXPECT_FALSE(task.isCanceled());
    EXPECT_EQ(runCount, 2);
}

int main(int argc, char **argv)
{
    ::testing::InitGoogleTest(&argc, argv);
    int result = RUN_ALL_TESTS();
    if (result == 0) {
        std::cout << "[timer_task] PASS" << std::endl;
    }
    return result;
}
