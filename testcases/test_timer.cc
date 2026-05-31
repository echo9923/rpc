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

TEST(TimerTest, OneShotEventRunsInReactorLoop)
{
    tinyrpc::Reactor reactor;
    int runCount = 0;
    auto event = std::make_shared<tinyrpc::TimerEvent>(10, false, [&runCount]() {
        ++runCount;
    });

    ASSERT_TRUE(reactor.getTimer()->addTimerEvent(event));
    waitLoop(&reactor, [&runCount]() { return runCount == 1; }, 5);

    EXPECT_EQ(runCount, 1);
    EXPECT_TRUE(event->isCanceled());
    EXPECT_EQ(reactor.getTimer()->getPendingEventCount(), 0);
}

TEST(TimerTest, RepeatedEventRunsUntilCanceled)
{
    tinyrpc::Reactor reactor;
    int runCount = 0;
    std::shared_ptr<tinyrpc::TimerEvent> event;
    event = std::make_shared<tinyrpc::TimerEvent>(5, true, [&runCount, &event]() {
        ++runCount;
        if (runCount >= 3) {
            event->cancel();
        }
    });

    ASSERT_TRUE(reactor.getTimer()->addTimerEvent(event));
    waitLoop(&reactor, [&runCount]() { return runCount >= 3; }, 10);

    EXPECT_EQ(runCount, 3);
    EXPECT_TRUE(event->isCanceled());
    EXPECT_EQ(reactor.getTimer()->getPendingEventCount(), 0);
}

TEST(TimerTest, MultipleEventsRunByExpireTime)
{
    tinyrpc::Reactor reactor;
    std::vector<std::string> order;
    auto earlyEvent = std::make_shared<tinyrpc::TimerEvent>(5, false, [&order]() {
        order.push_back("early");
    });
    auto lateEvent = std::make_shared<tinyrpc::TimerEvent>(25, false, [&order]() {
        order.push_back("late");
    });

    ASSERT_TRUE(reactor.getTimer()->addTimerEvent(lateEvent));
    ASSERT_TRUE(reactor.getTimer()->addTimerEvent(earlyEvent));
    waitLoop(&reactor, [&order]() { return order.size() == 2; }, 10);

    ASSERT_EQ(order.size(), 2);
    EXPECT_EQ(order[0], "early");
    EXPECT_EQ(order[1], "late");
}

TEST(TimerTest, DeletedEventDoesNotTrigger)
{
    tinyrpc::Reactor reactor;
    int runCount = 0;
    auto event = std::make_shared<tinyrpc::TimerEvent>(5, false, [&runCount]() {
        ++runCount;
    });

    ASSERT_TRUE(reactor.getTimer()->addTimerEvent(event));
    ASSERT_TRUE(reactor.getTimer()->delTimerEvent(event));

    int nfds = reactor.waitOnce(30);

    EXPECT_EQ(nfds, 0);
    EXPECT_EQ(runCount, 0);
    EXPECT_TRUE(event->isCanceled());
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
