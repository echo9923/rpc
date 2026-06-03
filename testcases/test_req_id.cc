/*
 * test_req_id.cc -- 任务四十：请求号工具与 TinyPbRpcController 语义测试。
 */

#include "comm/errorcode.h"
#include "comm/reqid.h"
#include "net/tinypb/tinypbrpccontroller.h"

#include <gtest/gtest.h>

#include <atomic>
#include <iostream>
#include <set>
#include <string>

TEST(ReqIdTest, genReqIdReturnsNonEmptyUniqueValues)
{
    std::set<std::string> values;

    for (int i = 0; i < 128; ++i) {
        std::string value = tinyrpc::ReqIdUtil::genReqId();
        EXPECT_FALSE(value.empty());
        EXPECT_TRUE(values.insert(value).second) << "duplicated reqId: " << value;
    }
}

TEST(TinyPbRpcControllerTest, ResetClearsErrorReqIdAndTimeout)
{
    tinyrpc::TinyPbRpcController controller;
    controller.setReqId("controller-msg-1");
    controller.setTimeout(3000);
    controller.setError(tinyrpc::ERROR_RPC_CHANNEL_NETWORK, "network failed");

    ASSERT_TRUE(controller.Failed());
    ASSERT_EQ(controller.getReqId(), "controller-msg-1");
    ASSERT_EQ(controller.getTimeout(), 3000);
    ASSERT_EQ(controller.getErrorCode(), tinyrpc::ERROR_RPC_CHANNEL_NETWORK);

    controller.Reset();

    EXPECT_FALSE(controller.Failed());
    EXPECT_TRUE(controller.ErrorText().empty());
    EXPECT_EQ(controller.getErrorCode(), 0);
    EXPECT_TRUE(controller.getReqId().empty());
    EXPECT_EQ(controller.getTimeout(), 0);
}

TEST(TinyPbRpcControllerTest, SetErrorStoresCodeAndText)
{
    tinyrpc::TinyPbRpcController controller;
    controller.setError(tinyrpc::ERROR_FAILED_DESERIALIZE, "bad response");

    EXPECT_TRUE(controller.Failed());
    EXPECT_EQ(controller.getErrorCode(), tinyrpc::ERROR_FAILED_DESERIALIZE);
    EXPECT_EQ(controller.ErrorText(), "bad response");
}

TEST(TinyPbRpcControllerTest, StartCancelRunsRegisteredCallbackOnce)
{
    tinyrpc::TinyPbRpcController controller;
    std::atomic<int> runCount {0};
    controller.setCancelCallback([&runCount]() {
        runCount.fetch_add(1);
    });

    controller.StartCancel();
    controller.StartCancel();

    EXPECT_TRUE(controller.IsCanceled());
    EXPECT_EQ(runCount.load(), 1);
}

TEST(TinyPbRpcControllerTest, ResetClearsCancelCallback)
{
    tinyrpc::TinyPbRpcController controller;
    std::atomic<int> runCount {0};
    controller.setCancelCallback([&runCount]() {
        runCount.fetch_add(1);
    });

    controller.Reset();
    controller.StartCancel();

    EXPECT_TRUE(controller.IsCanceled());
    EXPECT_EQ(runCount.load(), 0);
}

int main(int argc, char **argv)
{
    ::testing::InitGoogleTest(&argc, argv);
    int result = RUN_ALL_TESTS();
    if (result == 0) {
        std::cout << "[req_id] PASS" << std::endl;
    }
    return result;
}
