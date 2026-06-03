/*
 * test_coroutine_pool.cc -- 任务七十二：CoroutinePool 对象复用测试。
 */

#include "coroutine/coroutine.h"
#include "coroutine/coroutinepool.h"

#include <gtest/gtest.h>

#include <iostream>
#include <memory>
#include <vector>

TEST(CoroutinePoolTest, GetCoroutineRunsTask)
{
    tinyrpc::CoroutinePool pool(2);
    int value = 0;

    auto coroutine = pool.getCoroutine([&]() {
        value = 42;
    });

    ASSERT_NE(coroutine, nullptr);
    EXPECT_EQ(pool.getCreatedCount(), 1u);

    coroutine->resume();

    EXPECT_EQ(value, 42);
    EXPECT_EQ(coroutine->getState(), tinyrpc::CoroutineState::Finished);
}

TEST(CoroutinePoolTest, ReturnAndReuseCoroutineWithoutOldTaskPollution)
{
    tinyrpc::CoroutinePool pool(1);
    std::vector<int> order;

    auto first = pool.getCoroutine([&]() {
        order.push_back(1);
    });
    ASSERT_NE(first, nullptr);
    int firstId = first->getId();
    first->resume();
    ASSERT_TRUE(first->isFinished());

    ASSERT_TRUE(pool.returnCoroutine(std::move(first)));
    EXPECT_EQ(pool.getIdleCount(), 1u);

    auto second = pool.getCoroutine([&]() {
        order.push_back(2);
    });
    ASSERT_NE(second, nullptr);
    EXPECT_EQ(second->getId(), firstId);
    EXPECT_EQ(pool.getCreatedCount(), 1u);

    second->resume();

    EXPECT_EQ(order, std::vector<int>({1, 2}));
    EXPECT_TRUE(second->isFinished());
}

TEST(CoroutinePoolTest, ExhaustedPoolReturnsNullptr)
{
    tinyrpc::CoroutinePool pool(1);

    auto first = pool.getCoroutine([]() {});
    ASSERT_NE(first, nullptr);

    auto second = pool.getCoroutine([]() {});

    EXPECT_EQ(second, nullptr);
    EXPECT_EQ(pool.getCreatedCount(), 1u);
    EXPECT_EQ(pool.getIdleCount(), 0u);
}

TEST(CoroutinePoolTest, SuspendedCoroutineCannotBeReturned)
{
    tinyrpc::CoroutinePool pool(1);

    auto coroutine = pool.getCoroutine([]() {
        tinyrpc::Coroutine::yield();
    });
    ASSERT_NE(coroutine, nullptr);

    coroutine->resume();
    ASSERT_EQ(coroutine->getState(), tinyrpc::CoroutineState::Suspended);

    EXPECT_FALSE(pool.returnCoroutine(std::move(coroutine)));
    EXPECT_EQ(pool.getIdleCount(), 0u);
}

TEST(CoroutinePoolTest, ReadyCoroutineCanBeReturnedAndReused)
{
    tinyrpc::CoroutinePool pool(1);

    auto coroutine = pool.getCoroutine([]() {});
    ASSERT_NE(coroutine, nullptr);
    EXPECT_EQ(coroutine->getState(), tinyrpc::CoroutineState::Ready);

    ASSERT_TRUE(pool.returnCoroutine(std::move(coroutine)));

    bool ran = false;
    auto reused = pool.getCoroutine([&]() {
        ran = true;
    });
    ASSERT_NE(reused, nullptr);
    reused->resume();

    EXPECT_TRUE(ran);
    EXPECT_TRUE(reused->isFinished());
}

TEST(CoroutineTest, ResetFinishedCoroutineRunsNewTask)
{
    std::vector<int> order;
    tinyrpc::Coroutine coroutine([&]() {
        order.push_back(1);
    });

    coroutine.resume();
    ASSERT_TRUE(coroutine.isFinished());

    ASSERT_TRUE(coroutine.reset([&]() {
        order.push_back(2);
    }));
    EXPECT_EQ(coroutine.getState(), tinyrpc::CoroutineState::Ready);

    coroutine.resume();

    EXPECT_EQ(order, std::vector<int>({1, 2}));
    EXPECT_TRUE(coroutine.isFinished());
}

TEST(CoroutineTest, ResetSuspendedCoroutineFails)
{
    tinyrpc::Coroutine coroutine([]() {
        tinyrpc::Coroutine::yield();
    });

    coroutine.resume();
    ASSERT_EQ(coroutine.getState(), tinyrpc::CoroutineState::Suspended);

    EXPECT_FALSE(coroutine.reset([]() {}));
}

int main(int argc, char **argv)
{
    ::testing::InitGoogleTest(&argc, argv);
    int result = RUN_ALL_TESTS();
    if (result == 0) {
        std::cout << "[coroutinepool] PASS" << std::endl;
    }
    return result;
}
