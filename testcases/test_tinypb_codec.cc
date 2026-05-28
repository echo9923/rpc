/*
 * test_tinypb_codec.cc — 任务二十七/二十八：TinyPbCodec 编解码验收测试。
 *
 * 测试覆盖：
 *   1. GetProtocolType：确认返回 ProtocolType::TinyPb。
 *   2. EncodeWritesExpectedFrame：验证编码后帧的起止字节、字段顺序与内容。
 *   3. EncodeUsesNetworkByteOrder：验证 int32 字段以大端写入。
 *   4. EncodeBackfillsFields：验证编码后回填的长度字段和状态标记。
 *   5. EncodeRejectsInvalidData：验证各种非法输入时不污染 buffer 且 m_encodeSucc = false。
 *   6. DecodeRoundTripSingleFrame：encode 再 decode，验证全部字段一致，buffer 为空。
 *   7. DecodeParsesNetworkByteOrderFields：非零 errCode、非空 errInfo/pbData，验证字段正确。
 *   8. DecodeRejectsIncompleteFrameWithoutConsuming：截掉尾字节，decode 失败且不消费 buffer。
 *   9. DecodeRejectsBadStartOrEndWithoutConsuming：篡改起止符，decode 失败且不消费。
 *  10. DecodeRejectsInvalidDataType：传入非 TinyPbStruct，decode 失败且不消费。
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
// Test 6：encode → decode 往返，验证全部字段一致，buffer 最后为空
// ─────────────────────────────────────────────────────────────────────────────
TEST(TinyPbCodecTest, DecodeRoundTripSingleFrame)
{
    tinyrpc::TinyPbCodec codec;

    // 构造原始数据
    tinyrpc::TinyPbStruct original;
    original.m_msgReq = "req-roundtrip";
    original.m_serviceFullName = "OrderService.create";
    original.m_errCode = 42;
    original.m_errInfo = "service not found";
    original.m_pbData = "some-pb-data";

    // encode
    tinyrpc::TcpBuffer buffer(256);
    codec.encode(&buffer, &original);
    ASSERT_TRUE(original.m_encodeSucc);

    // decode 到新对象
    tinyrpc::TinyPbStruct decoded;
    codec.decode(&buffer, &decoded);

    // decode 成功
    EXPECT_TRUE(decoded.m_decodeSucc);

    // buffer 应被完全消费
    EXPECT_EQ(buffer.getReadableBytes(), 0u);

    // 所有字段应与原始一致（encode 回填后的值）
    EXPECT_EQ(decoded.m_pkLen, original.m_pkLen);
    EXPECT_EQ(decoded.m_msgReqLen, original.m_msgReqLen);
    EXPECT_EQ(decoded.m_msgReq, original.m_msgReq);
    EXPECT_EQ(decoded.m_serviceNameLen, original.m_serviceNameLen);
    EXPECT_EQ(decoded.m_serviceFullName, original.m_serviceFullName);
    EXPECT_EQ(decoded.m_errCode, original.m_errCode);
    EXPECT_EQ(decoded.m_errInfoLen, original.m_errInfoLen);
    EXPECT_EQ(decoded.m_errInfo, original.m_errInfo);
    EXPECT_EQ(decoded.m_pbData, original.m_pbData);
    EXPECT_EQ(decoded.m_checkNum, original.m_checkNum);
}

// ─────────────────────────────────────────────────────────────────────────────
// Test 7：decode 正确解析非零 errCode、非空 errInfo 和 pbData
// ─────────────────────────────────────────────────────────────────────────────
TEST(TinyPbCodecTest, DecodeParsesNetworkByteOrderFields)
{
    tinyrpc::TinyPbCodec codec;

    tinyrpc::TinyPbStruct original;
    original.m_msgReq = "req-netorder";
    original.m_serviceFullName = "Svc.method";
    original.m_errCode = 0x01020304;
    original.m_errInfo = "err detail";
    original.m_pbData = "\xAB\xCD\xEF";

    tinyrpc::TcpBuffer buffer(256);
    codec.encode(&buffer, &original);
    ASSERT_TRUE(original.m_encodeSucc);

    tinyrpc::TinyPbStruct decoded;
    codec.decode(&buffer, &decoded);

    EXPECT_TRUE(decoded.m_decodeSucc);
    EXPECT_EQ(decoded.m_errCode, 0x01020304);
    EXPECT_EQ(decoded.m_errInfo, "err detail");
    EXPECT_EQ(decoded.m_errInfoLen, static_cast<int32_t>(original.m_errInfo.size()));
    EXPECT_EQ(decoded.m_pbData, std::string("\xAB\xCD\xEF", 3));
    EXPECT_EQ(decoded.m_msgReq, "req-netorder");
    EXPECT_EQ(decoded.m_serviceFullName, "Svc.method");
    EXPECT_EQ(decoded.m_checkNum, 1);
}

// ─────────────────────────────────────────────────────────────────────────────
// Test 8：不完整帧（截掉最后一个字节），decode 失败且不消费 buffer
// ─────────────────────────────────────────────────────────────────────────────
TEST(TinyPbCodecTest, DecodeRejectsIncompleteFrameWithoutConsuming)
{
    tinyrpc::TinyPbCodec codec;

    tinyrpc::TinyPbStruct original;
    original.m_msgReq = "req-incomplete";
    original.m_serviceFullName = "Svc.m";

    tinyrpc::TcpBuffer buffer(256);
    codec.encode(&buffer, &original);
    ASSERT_TRUE(original.m_encodeSucc);

    // 截掉最后一个字节（PB_END）
    // 通过 retrieve 消费除最后 1 字节外的全部数据，再保留到一个新 buffer
    size_t fullSize = buffer.getReadableBytes();
    std::string truncated = buffer.retrieveAsString(fullSize - 1);
    // truncated 现在少了最后一个字节
    tinyrpc::TcpBuffer truncBuffer(256);
    truncBuffer.append(truncated);

    size_t before = truncBuffer.getReadableBytes();

    tinyrpc::TinyPbStruct decoded;
    codec.decode(&truncBuffer, &decoded);

    // decode 应失败
    EXPECT_FALSE(decoded.m_decodeSucc);

    // buffer 不应被消费
    EXPECT_EQ(truncBuffer.getReadableBytes(), before);
}

// ─────────────────────────────────────────────────────────────────────────────
// Test 9：篡改起始符或结束符，decode 失败且不消费 buffer
// ─────────────────────────────────────────────────────────────────────────────
TEST(TinyPbCodecTest, DecodeRejectsBadStartOrEndWithoutConsuming)
{
    tinyrpc::TinyPbCodec codec;

    tinyrpc::TinyPbStruct original;
    original.m_msgReq = "req-badmarker";
    original.m_serviceFullName = "Svc.m";

    // 9a. 篡改起始符
    {
        tinyrpc::TcpBuffer buffer(256);
        codec.encode(&buffer, &original);
        ASSERT_TRUE(original.m_encodeSucc);

        // 将 buffer 内容取出，篡改首字节，再放回
        std::string raw = buffer.retrieveAllAsString();
        raw[0] = 0x00; // 篡改 PB_START
        buffer.append(raw);

        size_t before = buffer.getReadableBytes();

        tinyrpc::TinyPbStruct decoded;
        codec.decode(&buffer, &decoded);

        EXPECT_FALSE(decoded.m_decodeSucc);
        EXPECT_EQ(buffer.getReadableBytes(), before);
    }

    // 9b. 篡改结束符
    {
        tinyrpc::TcpBuffer buffer(256);
        codec.encode(&buffer, &original);

        std::string raw = buffer.retrieveAllAsString();
        raw[raw.size() - 1] = 0x00; // 篡改 PB_END
        buffer.append(raw);

        size_t before = buffer.getReadableBytes();

        tinyrpc::TinyPbStruct decoded;
        codec.decode(&buffer, &decoded);

        EXPECT_FALSE(decoded.m_decodeSucc);
        EXPECT_EQ(buffer.getReadableBytes(), before);
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// Test 10：传入非 TinyPbStruct 的 AbstractData，decode 失败且不消费 buffer
// ─────────────────────────────────────────────────────────────────────────────
TEST(TinyPbCodecTest, DecodeRejectsInvalidDataType)
{
    tinyrpc::TinyPbCodec codec;

    tinyrpc::TinyPbStruct original;
    original.m_msgReq = "req-type";
    original.m_serviceFullName = "Svc.m";

    tinyrpc::TcpBuffer buffer(256);
    codec.encode(&buffer, &original);
    ASSERT_TRUE(original.m_encodeSucc);

    size_t before = buffer.getReadableBytes();

    // 用 AbstractData 代替 TinyPbStruct
    tinyrpc::AbstractData notPb;
    codec.decode(&buffer, &notPb);

    EXPECT_FALSE(notPb.m_decodeSucc);

    // buffer 不应被消费
    EXPECT_EQ(buffer.getReadableBytes(), before);
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
