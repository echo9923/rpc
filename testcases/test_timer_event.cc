/*
 * test_timer_event.cc -- 任务四十七：TimerEvent 与基础时间函数测试。
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

TEST(TimerEventTest, OneShotExpireTimeIsBasedOnCurrentTime)
{
    int64_t before = tinyrpc::getNowMs();
    tinyrpc::TimerEvent event(50, false, []() {});

    EXPECT_FALSE(event.isRepeated());
    EXPECT_FALSE(event.isCanceled());
    EXPECT_EQ(event.getIntervalMs(), 50);
    EXPECT_GE(event.getExpireTimeMs(), before);
    EXPECT_FALSE(event.isExpired(before));
    EXPECT_TRUE(event.isExpired(event.getExpireTimeMs()));
}

TEST(TimerEventTest, ResetTimeRefreshesExpireTimeAndInterval)
{
    tinyrpc::TimerEvent event(10, false, []() {});
    int64_t oldExpireTime = event.getExpireTimeMs();

    std::this_thread::sleep_for(std::chrono::milliseconds(2));
    event.resetTime(80);

    EXPECT_EQ(event.getIntervalMs(), 80);
    EXPECT_GT(event.getExpireTimeMs(), oldExpireTime);
    EXPECT_FALSE(event.isCanceled());
}

TEST(TimerEventTest, CancelPreventsCallbackExecution)
{
    int runCount = 0;
    tinyrpc::TimerEvent event(0, false, [&runCount]() {
        ++runCount;
    });

    event.cancel();
    event.run();

    EXPECT_TRUE(event.isCanceled());
    EXPECT_EQ(runCount, 0);
}

TEST(TimerEventTest, OneShotRunExecutesOnceAndCancels)
{
    int runCount = 0;
    tinyrpc::TimerEvent event(0, false, [&runCount]() {
        ++runCount;
    });

    event.run();
    event.run();

    EXPECT_TRUE(event.isCanceled());
    EXPECT_EQ(runCount, 1);
}

TEST(TimerEventTest, RepeatedRunResetsNextExpireTime)
{
    int runCount = 0;
    tinyrpc::TimerEvent event(10, true, [&runCount]() {
        ++runCount;
    });
    int64_t firstExpireTime = event.getExpireTimeMs();

    std::this_thread::sleep_for(std::chrono::milliseconds(2));
    event.run();

    EXPECT_FALSE(event.isCanceled());
    EXPECT_TRUE(event.isRepeated());
    EXPECT_EQ(runCount, 1);
    EXPECT_GT(event.getExpireTimeMs(), firstExpireTime);

    event.run();

    EXPECT_FALSE(event.isCanceled());
    EXPECT_EQ(runCount, 2);
}

int main(int argc, char **argv)
{
    ::testing::InitGoogleTest(&argc, argv);
    int result = RUN_ALL_TESTS();
    if (result == 0) {
        std::cout << "[timer_event] PASS" << std::endl;
    }
    return result;
}
