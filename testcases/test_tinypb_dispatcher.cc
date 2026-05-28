/*
 * test_tinypb_dispatcher.cc — 任务三十三：TinyPbDispatcher 验收测试。
 *
 * 测试覆盖：
 *   1. ParseServiceFullNameValid：合法 serviceFullName 正确拆分。
 *   2. ParseServiceFullNameRejectsInvalid：各种非法输入返回 false。
 *   3. DispatchWritesTinyPbResponse：dispatch 后 outputBuffer 中
 *      可解码出正确的响应，字段与请求一致。
 */

#include "net/reactor.h"
#include "net/tcpconnection.h"
#include "net/tinypb/tinypbcodec.h"
#include "net/tinypb/tinypbdispatcher.h"

#include <gtest/gtest.h>

#include <string>

// ─────────────────────────────────────────────────────────────────────────────
// Test 1：合法 serviceFullName 正确拆分
// ─────────────────────────────────────────────────────────────────────────────
TEST(TinyPbDispatcherTest, ParseServiceFullNameValid)
{
    tinyrpc::TinyPbDispatcher dispatcher;

    std::string serviceName;
    std::string methodName;

    EXPECT_TRUE(dispatcher.parseServiceFullName("QueryService.query_name", serviceName, methodName));
    EXPECT_EQ(serviceName, "QueryService");
    EXPECT_EQ(methodName, "query_name");
}

// ─────────────────────────────────────────────────────────────────────────────
// Test 2：各种非法输入返回 false
// ─────────────────────────────────────────────────────────────────────────────
TEST(TinyPbDispatcherTest, ParseServiceFullNameRejectsInvalid)
{
    tinyrpc::TinyPbDispatcher dispatcher;

    std::string serviceName;
    std::string methodName;

    // 空字符串
    EXPECT_FALSE(dispatcher.parseServiceFullName("", serviceName, methodName));

    // 没有 '.'
    EXPECT_FALSE(dispatcher.parseServiceFullName("NoDot", serviceName, methodName));

    // 以 '.' 开头，serviceName 为空
    EXPECT_FALSE(dispatcher.parseServiceFullName(".method", serviceName, methodName));

    // 以 '.' 结尾，methodName 为空
    EXPECT_FALSE(dispatcher.parseServiceFullName("Service.", serviceName, methodName));
}

// ─────────────────────────────────────────────────────────────────────────────
// Test 3：dispatch 后 outputBuffer 中可解码出正确的响应
// ─────────────────────────────────────────────────────────────────────────────
TEST(TinyPbDispatcherTest, DispatchWritesTinyPbResponse)
{
    // 创建 codec 和 dispatcher
    auto codec = std::make_shared<tinyrpc::TinyPbCodec>();
    auto dispatcher = std::make_shared<tinyrpc::TinyPbDispatcher>();

    // 创建 Reactor 和 TcpConnection（fd=-1 不对应真实 socket，仅用于测试）
    tinyrpc::Reactor reactor;
    auto conn = std::make_shared<tinyrpc::TcpConnection>(-1, &reactor, codec, dispatcher);

    // 构造请求
    tinyrpc::TinyPbStruct request;
    request.m_msgReq = "req-dispatch-001";
    request.m_serviceFullName = "QueryService.query_name";
    request.m_pbData = "\x08\x96\x01";
    request.m_errCode = 0;
    request.m_errInfo = "";

    // 调用 dispatch
    dispatcher->dispatch(&request, conn.get());

    // 从 outputBuffer 解码响应
    tinyrpc::TcpBuffer *outputBuf = conn->getOutputBuffer();
    ASSERT_GT(outputBuf->getReadableBytes(), 0u);

    tinyrpc::TinyPbStruct response;
    codec->decode(outputBuf, &response);
    ASSERT_TRUE(response.m_decodeSucc);

    // 验证响应字段与请求一致
    EXPECT_EQ(response.m_msgReq, "req-dispatch-001");
    EXPECT_EQ(response.m_serviceFullName, "QueryService.query_name");
    EXPECT_EQ(response.m_pbData, std::string("\x08\x96\x01", 3));
    EXPECT_EQ(response.m_errCode, 0);
    EXPECT_EQ(response.m_errInfo, "");

    // outputBuffer 应被完全消费
    EXPECT_EQ(outputBuf->getReadableBytes(), 0u);
}

// ─────────────────────────────────────────────────────────────────────────────
// main
// ─────────────────────────────────────────────────────────────────────────────
int main(int argc, char **argv)
{
    ::testing::InitGoogleTest(&argc, argv);
    int result = RUN_ALL_TESTS();
    if (result == 0) {
        std::cout << "[tinypb_dispatcher] PASS" << std::endl;
    }
    return result;
}
