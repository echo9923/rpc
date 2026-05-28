/*
 * test_tinypb_codec.cc — 任务二十七：TinyPbCodec encode 路径验收测试。
 *
 * 测试覆盖：
 *   1. GetProtocolType：确认返回 ProtocolType::TinyPb。
 *   2. EncodeWritesExpectedFrame：验证编码后帧的起止字节、字段顺序与内容。
 *   3. EncodeUsesNetworkByteOrder：验证 int32 字段以大端写入。
 *   4. EncodeBackfillsFields：验证编码后回填的长度字段和状态标记。
 *   5. EncodeRejectsInvalidData：验证各种非法输入时不污染 buffer 且 m_encodeSucc = false。
 */

#include "net/tcpbuffer.h"
#include "net/tinypb/tinypbcodec.h"

#include <arpa/inet.h>
#include <gtest/gtest.h>

#include <cstdint>
#include <cstring>
#include <string>

// ─────────────────────────────────────────────────────────────────────────────
// 辅助函数：从 TcpBuffer 偏移 offset 处读取 4 字节，用 ntohl 转回主机序
// ntohl: 将网络字节序（大端）的 uint32_t 转为主机字节序。
// ─────────────────────────────────────────────────────────────────────────────
static int32_t readNetInt32(const tinyrpc::TcpBuffer &buffer, size_t offset)
{
    const char *ptr = buffer.getReadPtr() + offset;
    uint32_t netValue;
    std::memcpy(&netValue, ptr, 4);
    return static_cast<int32_t>(ntohl(netValue));
}

// ─────────────────────────────────────────────────────────────────────────────
// Test 1：getProtocolType 返回 ProtocolType::TinyPb
// ─────────────────────────────────────────────────────────────────────────────
TEST(TinyPbCodecTest, GetProtocolType)
{
    tinyrpc::TinyPbCodec codec;
    EXPECT_EQ(codec.getProtocolType(), tinyrpc::ProtocolType::TinyPb);
}

// ─────────────────────────────────────────────────────────────────────────────
// Test 2：encode 写入完整的 TinyPB 帧，验证起止字节、包长、字段顺序和内容
// ─────────────────────────────────────────────────────────────────────────────
TEST(TinyPbCodecTest, EncodeWritesExpectedFrame)
{
    tinyrpc::TinyPbCodec codec;
    tinyrpc::TinyPbStruct pb;
    pb.m_msgReq = "req-001";
    pb.m_serviceFullName = "QueryService.query_name";
    pb.m_errCode = 0;
    pb.m_errInfo = "";
    pb.m_pbData = "\x08\x96\x01";

    tinyrpc::TcpBuffer buffer(256);
    codec.encode(&buffer, &pb);

    // 编码成功
    EXPECT_TRUE(pb.m_encodeSucc);

    // 总字节数应等于 m_pkLen
    EXPECT_EQ(buffer.getReadableBytes(), static_cast<size_t>(pb.m_pkLen));

    // 首字节 = PB_START (0x02)
    const char *raw = buffer.getReadPtr();
    EXPECT_EQ(static_cast<unsigned char>(raw[0]), 0x02);

    // 尾字节 = PB_END (0x03)
    EXPECT_EQ(static_cast<unsigned char>(raw[buffer.getReadableBytes() - 1]), 0x03);

    // pkLen 字段（offset=1, 4字节）
    int32_t pkLen = readNetInt32(buffer, 1);
    EXPECT_EQ(pkLen, pb.m_pkLen);

    // msgReqLen 字段（offset=5, 4字节）
    int32_t msgReqLen = readNetInt32(buffer, 5);
    EXPECT_EQ(msgReqLen, 7);

    // msgReq 内容（offset=9, 7字节）
    std::string msgReq(raw + 9, 7);
    EXPECT_EQ(msgReq, "req-001");

    // serviceNameLen 字段（offset=16, 4字节）
    int32_t serviceNameLen = readNetInt32(buffer, 16);
    EXPECT_EQ(serviceNameLen, 23);

    // serviceFullName 内容（offset=20, 23字节）
    std::string serviceName(raw + 20, 23);
    EXPECT_EQ(serviceName, "QueryService.query_name");

    // errCode 字段（offset=43, 4字节）
    int32_t errCode = readNetInt32(buffer, 43);
    EXPECT_EQ(errCode, 0);

    // errInfoLen 字段（offset=47, 4字节）
    int32_t errInfoLen = readNetInt32(buffer, 47);
    EXPECT_EQ(errInfoLen, 0);

    // errInfo 为空，所以 pbData 紧接其后（offset=51）
    // pbData 长度为 3
    std::string pbData(raw + 51, 3);
    EXPECT_EQ(pbData, std::string("\x08\x96\x01", 3));

    // checkNum 字段（offset=54, 4字节）
    int32_t checkNum = readNetInt32(buffer, 54);
    EXPECT_EQ(checkNum, 1);

    // PB_END（offset=58）
    EXPECT_EQ(static_cast<unsigned char>(raw[58]), 0x03);
}

// ─────────────────────────────────────────────────────────────────────────────
// Test 3：验证 int32 字段以网络字节序（大端）写入
// ─────────────────────────────────────────────────────────────────────────────
TEST(TinyPbCodecTest, EncodeUsesNetworkByteOrder)
{
    tinyrpc::TinyPbCodec codec;
    tinyrpc::TinyPbStruct pb;
    pb.m_msgReq = "req-002";
    pb.m_serviceFullName = "Svc.method";
    pb.m_errCode = 0x01020304;
    pb.m_errInfo = "";
    pb.m_pbData = "";

    tinyrpc::TcpBuffer buffer(256);
    codec.encode(&buffer, &pb);

    const char *raw = buffer.getReadPtr();

    // 验证 pkLen 的字节顺序（大端）
    // pkLen 的值在 encode 中计算，我们直接验证 ntohl 能还原它
    uint32_t netPkLen;
    std::memcpy(&netPkLen, raw + 1, 4);
    EXPECT_EQ(static_cast<int32_t>(ntohl(netPkLen)), pb.m_pkLen);

    // 验证 errCode = 0x01020304 的字节序
    // 大端存储应为 01 02 03 04
    int32_t errCodeHost = 0x01020304;
    uint32_t errCodeNet;
    std::memcpy(&errCodeNet, raw + 1 + 4 + 4 + 7 + 4 + 10, 4);
    EXPECT_EQ(static_cast<int32_t>(ntohl(errCodeNet)), errCodeHost);

    // 独立验证：直接比较原始字节
    unsigned char expected[4] = {0x01, 0x02, 0x03, 0x04};
    const unsigned char *errPtr = reinterpret_cast<const unsigned char *>(
        raw + 1 + 4 + 4 + 7 + 4 + 10);
    for (int i = 0; i < 4; ++i) {
        EXPECT_EQ(errPtr[i], expected[i]);
    }

    // 验证 msgReqLen 的网络序
    uint32_t msgReqLenNet;
    std::memcpy(&msgReqLenNet, raw + 5, 4);
    EXPECT_EQ(static_cast<int32_t>(ntohl(msgReqLenNet)), 7);

    // 验证 serviceNameLen 的网络序
    uint32_t svcNameLenNet;
    std::memcpy(&svcNameLenNet, raw + 1 + 4 + 4 + 7, 4);
    EXPECT_EQ(static_cast<int32_t>(ntohl(svcNameLenNet)), 10);

    // 验证 errInfoLen 的网络序（errInfo 为空，值为 0）
    size_t errInfoLenOffset = 1 + 4 + 4 + 7 + 4 + 10 + 4;
    uint32_t errInfoLenNet;
    std::memcpy(&errInfoLenNet, raw + errInfoLenOffset, 4);
    EXPECT_EQ(static_cast<int32_t>(ntohl(errInfoLenNet)), 0);

    // 验证 checkNum 的网络序
    // checkNum 在 pbData 之后，pbData 为空
    size_t checkNumOffset = errInfoLenOffset + 4 + 0;
    uint32_t checkNumNet;
    std::memcpy(&checkNumNet, raw + checkNumOffset, 4);
    EXPECT_EQ(static_cast<int32_t>(ntohl(checkNumNet)), 1);
}

// ─────────────────────────────────────────────────────────────────────────────
// Test 4：验证编码后回填的长度字段和状态标记
// ─────────────────────────────────────────────────────────────────────────────
TEST(TinyPbCodecTest, EncodeBackfillsFields)
{
    tinyrpc::TinyPbCodec codec;
    tinyrpc::TinyPbStruct pb;
    pb.m_msgReq = "req-003";
    pb.m_serviceFullName = "OrderService.create";
    pb.m_errCode = 42;
    pb.m_errInfo = "service not found";
    pb.m_pbData = "some-pb-data";

    tinyrpc::TcpBuffer buffer(256);
    codec.encode(&buffer, &pb);

    // m_encodeSucc 应为 true
    EXPECT_TRUE(pb.m_encodeSucc);

    // m_pkLen 应 > 0 且等于 buffer 可读字节数
    EXPECT_GT(pb.m_pkLen, 0);
    EXPECT_EQ(static_cast<size_t>(pb.m_pkLen), buffer.getReadableBytes());

    // 长度字段回填
    EXPECT_EQ(pb.m_msgReqLen, static_cast<int32_t>(pb.m_msgReq.size()));
    EXPECT_EQ(pb.m_serviceNameLen, static_cast<int32_t>(pb.m_serviceFullName.size()));
    EXPECT_EQ(pb.m_errInfoLen, static_cast<int32_t>(pb.m_errInfo.size()));

    // m_checkNum 固定为 1
    EXPECT_EQ(pb.m_checkNum, 1);
}

// ─────────────────────────────────────────────────────────────────────────────
// Test 5：非法输入时不污染 buffer 且 m_encodeSucc = false
// ─────────────────────────────────────────────────────────────────────────────
TEST(TinyPbCodecTest, EncodeRejectsInvalidData)
{
    tinyrpc::TinyPbCodec codec;

    // 5a. buffer == nullptr
    {
        tinyrpc::TinyPbStruct pb;
        pb.m_msgReq = "req";
        pb.m_serviceFullName = "Svc.m";
        codec.encode(nullptr, &pb);
        EXPECT_FALSE(pb.m_encodeSucc);
    }

    // 5b. data == nullptr
    {
        tinyrpc::TcpBuffer buffer(64);
        size_t before = buffer.getReadableBytes();
        codec.encode(&buffer, nullptr);
        EXPECT_EQ(buffer.getReadableBytes(), before);
    }

    // 5c. data 不是 TinyPbStruct（用 AbstractData 直接构造）
    {
        tinyrpc::TcpBuffer buffer(64);
        tinyrpc::AbstractData notPb;
        codec.encode(&buffer, &notPb);
        EXPECT_FALSE(notPb.m_encodeSucc);
        EXPECT_EQ(buffer.getReadableBytes(), 0u);
    }

    // 5d. m_msgReq 为空
    {
        tinyrpc::TcpBuffer buffer(64);
        tinyrpc::TinyPbStruct pb;
        pb.m_msgReq = "";
        pb.m_serviceFullName = "Svc.m";
        codec.encode(&buffer, &pb);
        EXPECT_FALSE(pb.m_encodeSucc);
        EXPECT_EQ(buffer.getReadableBytes(), 0u);
    }

    // 5e. m_serviceFullName 为空
    {
        tinyrpc::TcpBuffer buffer(64);
        tinyrpc::TinyPbStruct pb;
        pb.m_msgReq = "req";
        pb.m_serviceFullName = "";
        codec.encode(&buffer, &pb);
        EXPECT_FALSE(pb.m_encodeSucc);
        EXPECT_EQ(buffer.getReadableBytes(), 0u);
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// main
// ─────────────────────────────────────────────────────────────────────────────
int main(int argc, char **argv)
{
    ::testing::InitGoogleTest(&argc, argv);
    int result = RUN_ALL_TESTS();
    if (result == 0) {
        std::cout << "[tinypb_codec] PASS" << std::endl;
    }
    return result;
}
