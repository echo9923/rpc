/*
 * test_abstract_codec.cc — 任务二十五：AbstractData / AbstractCodec 接口验收测试。
 *
 * 测试覆盖：
 *   1. AbstractDataDefaults：验证 m_encodeSucc / m_decodeSucc 默认为 false。
 *   2. StringCodecEncode：StringCodec::encode 将 payload 写入 TcpBuffer，
 *      并设置 m_encodeSucc = true。
 *   3. StringCodecDecode：StringCodec::decode 从 TcpBuffer 取出全部可读数据
 *      写回 payload，并设置 m_decodeSucc = true。
 *   4. GetProtocolType：验证 StringCodec 返回预期的 ProtocolType。
 */

#include "net/abstractcodec.h"

#include <gtest/gtest.h>

#include <string>

// ─────────────────────────────────────────────────────────────────────────────
// 测试辅助类：StringData，承载纯文本载荷。
// 继承 AbstractData，添加 m_payload 字段用于存储编解码的字符串内容。
// ─────────────────────────────────────────────────────────────────────────────
class StringData : public tinyrpc::AbstractData {
 public:
    std::string m_payload;
};

// ─────────────────────────────────────────────────────────────────────────────
// 测试辅助类：StringCodec，实现最简单的字符串编解码。
// encode：将 StringData::m_payload 追加到 TcpBuffer。
// decode：从 TcpBuffer 取出全部可读数据写回 StringData::m_payload。
// ─────────────────────────────────────────────────────────────────────────────
class StringCodec : public tinyrpc::AbstractCodec {
 public:
    // 将 data->payload 的内容追加写入 buffer。
    // 调用前通过 static_cast 将 AbstractData* 转为 StringData*。
    // 写入成功后将 data->m_encodeSucc 置为 true。
    void encode(tinyrpc::TcpBuffer *buffer, tinyrpc::AbstractData *data) override
    {
        auto *sd = static_cast<StringData *>(data);
        buffer->append(sd->m_payload);
        sd->m_encodeSucc = true;
    }

    // 从 buffer 取出全部可读数据，写入 data->payload。
    // 调用 retrieveAllAsString 会消费 buffer 中的数据并重置读写指针。
    // 读取成功后将 data->m_decodeSucc 置为 true。
    void decode(tinyrpc::TcpBuffer *buffer, tinyrpc::AbstractData *data) override
    {
        auto *sd = static_cast<StringData *>(data);
        sd->m_payload = buffer->retrieveAllAsString();
        sd->m_decodeSucc = true;
    }

    // 返回协议类型，此处固定返回 TinyPb，仅用于验证 getProtocolType 接口。
    tinyrpc::ProtocolType getProtocolType() const override
    {
        return tinyrpc::ProtocolType::TinyPb;
    }
};

// ─────────────────────────────────────────────────────────────────────────────
// Test 1：AbstractData 默认状态
// ─────────────────────────────────────────────────────────────────────────────
TEST(AbstractCodecTest, AbstractDataDefaults)
{
    StringData data;
    // 新构造的 AbstractData，m_encodeSucc 和 m_decodeSucc 均应为 false
    EXPECT_FALSE(data.m_encodeSucc);
    EXPECT_FALSE(data.m_decodeSucc);
}

// ─────────────────────────────────────────────────────────────────────────────
// Test 2：StringCodec::encode 将 payload 写入 TcpBuffer 并设置 m_encodeSucc
// ─────────────────────────────────────────────────────────────────────────────
TEST(AbstractCodecTest, StringCodecEncode)
{
    StringCodec codec;
    StringData data;
    data.m_payload = "hello codec";

    tinyrpc::TcpBuffer buffer(64);

    // encode 前状态
    EXPECT_FALSE(data.m_encodeSucc);

    codec.encode(&buffer, &data);

    // encode 后 m_encodeSucc 应为 true
    EXPECT_TRUE(data.m_encodeSucc);

    // buffer 中应有 "hello codec" 共 11 字节
    EXPECT_EQ(buffer.getReadableBytes(), data.m_payload.size());
    EXPECT_EQ(buffer.retrieveAllAsString(), "hello codec");
}

// ─────────────────────────────────────────────────────────────────────────────
// Test 3：StringCodec::decode 从 TcpBuffer 读取数据到 payload 并设置 m_decodeSucc
// ─────────────────────────────────────────────────────────────────────────────
TEST(AbstractCodecTest, StringCodecDecode)
{
    StringCodec codec;
    tinyrpc::TcpBuffer buffer(64);
    buffer.append("decode test");

    StringData data;

    // decode 前状态
    EXPECT_FALSE(data.m_decodeSucc);
    EXPECT_TRUE(data.m_payload.empty());

    codec.decode(&buffer, &data);

    // decode 后 m_decodeSucc 应为 true，m_payload 应为 "decode test"
    EXPECT_TRUE(data.m_decodeSucc);
    EXPECT_EQ(data.m_payload, "decode test");

    // buffer 应被完全消费
    EXPECT_EQ(buffer.getReadableBytes(), 0u);
}

// ─────────────────────────────────────────────────────────────────────────────
// Test 4：getProtocolType 返回预期的枚举值
// ─────────────────────────────────────────────────────────────────────────────
TEST(AbstractCodecTest, GetProtocolType)
{
    StringCodec codec;
    EXPECT_EQ(codec.getProtocolType(), tinyrpc::ProtocolType::TinyPb);
}

// ─────────────────────────────────────────────────────────────────────────────
// main
// ─────────────────────────────────────────────────────────────────────────────
int main(int argc, char **argv)
{
    ::testing::InitGoogleTest(&argc, argv);
    int result = RUN_ALL_TESTS();
    if (result == 0) {
        std::cout << "[abstract_codec] PASS" << std::endl;
    }
    return result;
}
