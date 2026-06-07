/*
 * test_coroutine_pool.cc -- 任务七十二：CoroutinePool 对象复用测试。
 */

#include "coroutine/coroutine.h"
#include "coroutine/coroutinepool.h"
#include "comm/config.h"

#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

namespace {

std::string writeTempConfig(const std::string& name, const std::string& content)
{
    std::filesystem::create_directories("build/coroutine-pool-tests");
    std::string path = "build/coroutine-pool-tests/" + name;
    std::ofstream output(path);
    output << content;
    return path;
}

}  // namespace

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

TEST(CoroutinePoolTest, DefaultConfigInitializesPoolAndReturnsNullWhenExhausted)
{
    tinyrpc::Config config;
    tinyrpc::CoroutinePool pool(config);

    EXPECT_EQ(pool.getInitialCapacity(), 128u);
    EXPECT_EQ(pool.getCapacity(), 128u);

    std::vector<std::unique_ptr<tinyrpc::Coroutine>> coroutines;
    coroutines.reserve(128);
    for (int i = 0; i < 128; ++i) {
        auto coroutine = pool.getCoroutine([]() {});
        ASSERT_NE(coroutine, nullptr);
        coroutines.push_back(std::move(coroutine));
    }

    auto exhausted = pool.getCoroutine([]() {});

    EXPECT_EQ(exhausted, nullptr);
    EXPECT_EQ(pool.getCreatedCount(), 128u);
    EXPECT_EQ(pool.getCapacity(), 128u);
}

TEST(CoroutinePoolTest, ConfigCanExpandWhenInitialCapacityIsExhausted)
{
    tinyrpc::Config config;
    std::string path = writeTempConfig(
        "task93_expand.xml",
        "<config>"
        "    <server>"
        "        <host>127.0.0.1</host>"
        "        <port>25007</port>"
        "        <protocol>tinypb</protocol>"
        "    </server>"
        "    <coroutine>"
        "        <pool_size>2</pool_size>"
        "        <stack_size_kb>64</stack_size_kb>"
        "        <expand_on_exhausted>true</expand_on_exhausted>"
        "    </coroutine>"
        "</config>"
    );

    ASSERT_TRUE(config.loadFromXml(path));
    tinyrpc::CoroutinePool pool(config);

    auto first = pool.getCoroutine([]() {});
    auto second = pool.getCoroutine([]() {});
    auto third = pool.getCoroutine([]() {});

    ASSERT_NE(first, nullptr);
    ASSERT_NE(second, nullptr);
    ASSERT_NE(third, nullptr);
    EXPECT_EQ(pool.getInitialCapacity(), 2u);
    EXPECT_EQ(pool.getCapacity(), 4u);
    EXPECT_EQ(pool.getCreatedCount(), 3u);
}

TEST(CoroutinePoolTest, ExpandedCoroutineCanBeReturnedAndReused)
{
    tinyrpc::CoroutinePool pool(
        1,
        128 * 1024,
        tinyrpc::CoroutinePoolExhaustPolicy::ExpandBlock
    );

    auto first = pool.getCoroutine([]() {});
    auto expanded = pool.getCoroutine([]() {});
    ASSERT_NE(first, nullptr);
    ASSERT_NE(expanded, nullptr);
    int expandedId = expanded->getId();

    expanded->resume();
    ASSERT_TRUE(expanded->isFinished());
    ASSERT_TRUE(pool.returnCoroutine(std::move(expanded)));
    EXPECT_EQ(pool.getIdleCount(), 1u);

    bool ran = false;
    auto reused = pool.getCoroutine([&]() {
        ran = true;
    });

    ASSERT_NE(reused, nullptr);
    EXPECT_EQ(reused->getId(), expandedId);
    EXPECT_EQ(pool.getCreatedCount(), 2u);

    reused->resume();
    EXPECT_TRUE(ran);
    EXPECT_TRUE(reused->isFinished());
}

TEST(CoroutinePoolTest, ExpandPolicySupportsZeroInitialCapacity)
{
    tinyrpc::CoroutinePool pool(
        0,
        128 * 1024,
        tinyrpc::CoroutinePoolExhaustPolicy::ExpandBlock
    );

    auto coroutine = pool.getCoroutine([]() {});

    ASSERT_NE(coroutine, nullptr);
    EXPECT_EQ(pool.getInitialCapacity(), 0u);
    EXPECT_EQ(pool.getCapacity(), 1u);
    EXPECT_EQ(pool.getCreatedCount(), 1u);
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
