/*
 * test_hook_sleep.cc -- 任务七十：sleep/usleep hook 的协程恢复测试。
 */

#include "coroutine/coroutine.h"
#include "coroutine/coroutine_hook.h"
#include "net/reactor.h"
#include "net/timer.h"

#include <gtest/gtest.h>

#include <cstdint>
#include <functional>
#include <iostream>
#include <vector>

namespace {

void driveReactorUntil(tinyrpc::Reactor *reactor, const std::function<bool()>& done, int timeoutMs)
{
    int64_t deadline = tinyrpc::getNowMs() + timeoutMs;
    while (!done() && tinyrpc::getNowMs() < deadline) {
        reactor->waitOnce(100);
    }
}

}

TEST(HookSleepTest, MainCoroutineSleepUsesRawSleep)
{
    tinyrpc::Reactor reactor;
    int64_t startMs = tinyrpc::getNowMs();

    unsigned int remaining = tinyrpc::sleep_hook(&reactor, 0);

    EXPECT_EQ(remaining, 0u);
    EXPECT_LT(tinyrpc::getNowMs() - startMs, 50);
}

TEST(HookSleepTest, UsleepHookYieldsAndTimerResumesCoroutine)
{
    tinyrpc::Reactor reactor;
    bool afterSleep = false;
    int64_t startMs = tinyrpc::getNowMs();
    int64_t resumedMs = 0;

    tinyrpc::Coroutine co([&]() {
        int ret = tinyrpc::usleep_hook(&reactor, 20 * 1000);
        EXPECT_EQ(ret, 0);
        resumedMs = tinyrpc::getNowMs();
        afterSleep = true;
    });

    co.resume();

    ASSERT_EQ(co.getState(), tinyrpc::CoroutineState::Suspended);
    EXPECT_FALSE(afterSleep);

    driveReactorUntil(&reactor, [&afterSleep]() { return afterSleep; }, 1000);

    ASSERT_TRUE(afterSleep);
    EXPECT_EQ(co.getState(), tinyrpc::CoroutineState::Finished);
    EXPECT_GE(resumedMs - startMs, 15);
    EXPECT_LT(resumedMs - startMs, 250);
}

TEST(HookSleepTest, SleepHookYieldsAndTimerResumesCoroutine)
{
    tinyrpc::Reactor reactor;
    bool afterSleep = false;
    int64_t startMs = tinyrpc::getNowMs();
    int64_t resumedMs = 0;

    tinyrpc::Coroutine co([&]() {
        unsigned int remaining = tinyrpc::sleep_hook(&reactor, 1);
        EXPECT_EQ(remaining, 0u);
        resumedMs = tinyrpc::getNowMs();
        afterSleep = true;
    });

    co.resume();

    ASSERT_EQ(co.getState(), tinyrpc::CoroutineState::Suspended);
    EXPECT_FALSE(afterSleep);

    driveReactorUntil(&reactor, [&afterSleep]() { return afterSleep; }, 2000);

    ASSERT_TRUE(afterSleep);
    EXPECT_EQ(co.getState(), tinyrpc::CoroutineState::Finished);
    EXPECT_GE(resumedMs - startMs, 950);
    EXPECT_LT(resumedMs - startMs, 1800);
}

TEST(HookSleepTest, OneSleepingCoroutineDoesNotBlockAnotherCoroutine)
{
    tinyrpc::Reactor reactor;
    std::vector<int> order;

    tinyrpc::Coroutine sleeper([&]() {
        order.push_back(1);
        tinyrpc::usleep_hook(&reactor, 40 * 1000);
        order.push_back(3);
    });
    tinyrpc::Coroutine runner([&]() {
        order.push_back(2);
    });

    sleeper.resume();
    ASSERT_EQ(sleeper.getState(), tinyrpc::CoroutineState::Suspended);

    runner.resume();
    EXPECT_EQ(order, std::vector<int>({1, 2}));

    driveReactorUntil(&reactor, [&sleeper]() { return sleeper.isFinished(); }, 1000);

    ASSERT_TRUE(sleeper.isFinished());
    EXPECT_EQ(order, std::vector<int>({1, 2, 3}));
}

TEST(HookSleepTest, MultipleSleepingCoroutinesResumeByTimerOrder)
{
    tinyrpc::Reactor reactor;
    std::vector<int> order;

    tinyrpc::Coroutine late([&]() {
        tinyrpc::usleep_hook(&reactor, 50 * 1000);
        order.push_back(2);
    });
    tinyrpc::Coroutine early([&]() {
        tinyrpc::usleep_hook(&reactor, 10 * 1000);
        order.push_back(1);
    });

    late.resume();
    early.resume();

    ASSERT_EQ(late.getState(), tinyrpc::CoroutineState::Suspended);
    ASSERT_EQ(early.getState(), tinyrpc::CoroutineState::Suspended);

    driveReactorUntil(&reactor, [&order]() { return order.size() == 2; }, 1000);

    ASSERT_EQ(order.size(), 2u);
    EXPECT_EQ(order[0], 1);
    EXPECT_EQ(order[1], 2);
}

int main(int argc, char **argv)
{
    ::testing::InitGoogleTest(&argc, argv);
    int result = RUN_ALL_TESTS();
    if (result == 0) {
        std::cout << "[hook_sleep] PASS" << std::endl;
    }
    return result;
}
