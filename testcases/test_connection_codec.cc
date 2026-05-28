/*
 * test_connection_codec.cc — 任务三十二：连接层协议接入验收测试。
 *
 * 本测试验证 TcpConnection::execute() 中使用的 decode→encode 回环模式：
 *   1. CodecRoundTrip：encode 一帧，decode 后拿到与输入一致的字段，
 *      再 encode 回第二帧，两次编码的输出字节完全一致。
 *   2. PartialFrameNoConsume：半包时 decode 失败且不消费 buffer。
 *
 * 此模式与 execute() 内部逻辑等价（有 codec 时循环 decode→encode）。
 * 不需要访问 TcpConnection 的私有成员。
 */

#include "net/tcpbuffer.h"
#include "net/tinypb/tinypbcodec.h"

#include <gtest/gtest.h>

#include <string>

// ─────────────────────────────────────────────────────────────────────────────
// Test 1：encode → decode → re-encode 两次编码字节完全一致
// 对应 execute() 中 "decode 拿到一帧 → encode 回一帧" 的逻辑。
// ─────────────────────────────────────────────────────────────────────────────
TEST(ConnectionCodecTest, CodecRoundTrip)
{
    tinyrpc::TinyPbCodec codec;

    // 构造原始请求
    tinyrpc::TinyPbStruct request;
    request.m_msgReq = "req-conn-001";
    request.m_serviceFullName = "QueryService.query_name";
    request.m_errCode = 0;
    request.m_errInfo = "";
    request.m_pbData = "\x08\x96\x01";

    // 第一次 encode（模拟上游写入 inputBuffer）
    tinyrpc::TcpBuffer sendBuf(256);
    codec.encode(&sendBuf, &request);
    ASSERT_TRUE(request.m_encodeSucc);
    std::string firstEncoded = sendBuf.retrieveAllAsString();

    // decode（模拟 execute 中的 decode）
    tinyrpc::TcpBuffer inputBuf(256);
    inputBuf.append(firstEncoded);
    tinyrpc::TinyPbStruct decoded;
    codec.decode(&inputBuf, &decoded);
    ASSERT_TRUE(decoded.m_decodeSucc);

    // 校验 decode 字段与原输入一致
    EXPECT_EQ(decoded.m_msgReq, "req-conn-001");
    EXPECT_EQ(decoded.m_serviceFullName, "QueryService.query_name");
    EXPECT_EQ(decoded.m_errCode, 0);
    EXPECT_EQ(decoded.m_errInfo, "");
    EXPECT_EQ(decoded.m_pbData, std::string("\x08\x96\x01", 3));

    // 第二次 encode（模拟 execute 中的 encode 到 outputBuffer）
    tinyrpc::TcpBuffer outputBuf(256);
    codec.encode(&outputBuf, &decoded);
    ASSERT_TRUE(decoded.m_encodeSucc);
    std::string secondEncoded = outputBuf.retrieveAllAsString();

    // 两次编码的字节完全一致
    EXPECT_EQ(firstEncoded, secondEncoded);

    // inputBuf 应被完全消费
    EXPECT_EQ(inputBuf.getReadableBytes(), 0u);
}

// ─────────────────────────────────────────────────────────────────────────────
// Test 2：半包不消费
// 对应 execute() 中 decode 失败时 break，不消费 buffer 等下一轮 input()。
// ─────────────────────────────────────────────────────────────────────────────
TEST(ConnectionCodecTest, PartialFrameNoConsume)
{
    tinyrpc::TinyPbCodec codec;

    // 构造完整帧
    tinyrpc::TinyPbStruct original;
    original.m_msgReq = "req-partial";
    original.m_serviceFullName = "Svc.partial";

    tinyrpc::TcpBuffer encodeBuf(256);
    codec.encode(&encodeBuf, &original);
    ASSERT_TRUE(original.m_encodeSucc);
    std::string fullFrame = encodeBuf.retrieveAllAsString();

    // 截掉最后一字节作为半包
    std::string partialFrame = fullFrame.substr(0, fullFrame.size() - 1);

    tinyrpc::TcpBuffer inputBuf(256);
    inputBuf.append(partialFrame);

    // decode 应失败
    tinyrpc::TinyPbStruct decoded;
    codec.decode(&inputBuf, &decoded);
    EXPECT_FALSE(decoded.m_decodeSucc);

    // buffer 不应被消费
    EXPECT_EQ(inputBuf.getReadableBytes(), partialFrame.size());
}

// ─────────────────────────────────────────────────────────────────────────────
// main
// ─────────────────────────────────────────────────────────────────────────────
int main(int argc, char **argv)
{
    ::testing::InitGoogleTest(&argc, argv);
    int result = RUN_ALL_TESTS();
    if (result == 0) {
        std::cout << "[connection_codec] PASS" << std::endl;
    }
    return result;
}
