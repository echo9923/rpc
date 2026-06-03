/*
 * test_timer.cc -- 任务四十八：Timer + timerfd 接入 Reactor 测试。
 */

#include "net/reactor.h"
#include "net/timer.h"

#include <gtest/gtest.h>

#include <chrono>
#include <functional>
#include <iostream>
#include <memory>
#include <string>
#include <thread>
#include <vector>

namespace {

void waitLoop(tinyrpc::Reactor *reactor, const std::function<bool()>& done, int maxRounds)
{
    for (int i = 0; i < maxRounds && !done(); ++i) {
        reactor->waitOnce(100);
    }
}

}

TEST(TimerTest, ReactorOwnsTimer)
{
    tinyrpc::Reactor reactor;

    ASSERT_NE(reactor.getTimer(), nullptr);
    EXPECT_GE(reactor.getTimer()->getFd(), 0);
}

TEST(TimerTest, OneShotTaskRunsInReactorLoop)
{
    tinyrpc::Reactor reactor;
    int runCount = 0;
    auto task = std::make_shared<tinyrpc::TimerTask>(10, false, [&runCount]() {
        ++runCount;
    });

    ASSERT_TRUE(reactor.getTimer()->addTimerTask(task));
    waitLoop(&reactor, [&runCount]() { return runCount == 1; }, 5);

    EXPECT_EQ(runCount, 1);
    EXPECT_TRUE(task->isCanceled());
    EXPECT_EQ(reactor.getTimer()->getPendingTaskCount(), 0);
}

TEST(TimerTest, RepeatedTaskRunsUntilCanceled)
{
    tinyrpc::Reactor reactor;
    int runCount = 0;
    std::shared_ptr<tinyrpc::TimerTask> task;
    task = std::make_shared<tinyrpc::TimerTask>(5, true, [&runCount, &task]() {
        ++runCount;
        if (runCount >= 3) {
            task->cancel();
        }
    });

    ASSERT_TRUE(reactor.getTimer()->addTimerTask(task));
    waitLoop(&reactor, [&runCount]() { return runCount >= 3; }, 10);

    EXPECT_EQ(runCount, 3);
    EXPECT_TRUE(task->isCanceled());
    EXPECT_EQ(reactor.getTimer()->getPendingTaskCount(), 0);
}

TEST(TimerTest, MultipleTasksRunByExpireTime)
{
    tinyrpc::Reactor reactor;
    std::vector<std::string> order;
    auto earlyTask = std::make_shared<tinyrpc::TimerTask>(5, false, [&order]() {
        order.push_back("early");
    });
    auto lateTask = std::make_shared<tinyrpc::TimerTask>(25, false, [&order]() {
        order.push_back("late");
    });

    ASSERT_TRUE(reactor.getTimer()->addTimerTask(lateTask));
    ASSERT_TRUE(reactor.getTimer()->addTimerTask(earlyTask));
    waitLoop(&reactor, [&order]() { return order.size() == 2; }, 10);

    ASSERT_EQ(order.size(), 2);
    EXPECT_EQ(order[0], "early");
    EXPECT_EQ(order[1], "late");
}

TEST(TimerTest, DeletedTaskDoesNotTrigger)
{
    tinyrpc::Reactor reactor;
    int runCount = 0;
    auto task = std::make_shared<tinyrpc::TimerTask>(5, false, [&runCount]() {
        ++runCount;
    });

    ASSERT_TRUE(reactor.getTimer()->addTimerTask(task));
    ASSERT_TRUE(reactor.getTimer()->delTimerTask(task));

    int nfds = reactor.waitOnce(30);

    EXPECT_EQ(nfds, 0);
    EXPECT_EQ(runCount, 0);
    EXPECT_TRUE(task->isCanceled());
}

int main(int argc, char **argv)
{
    ::testing::InitGoogleTest(&argc, argv);
    int result = RUN_ALL_TESTS();
    if (result == 0) {
        std::cout << "[timer] PASS" << std::endl;
    }
    return result;
}
