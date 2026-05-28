/*
 * test_tinypb_data.cc — 任务二十六：TinyPbStruct 数据结构验收测试。
 *
 * 测试覆盖：
 *   1. DefaultValues：验证所有字段默认值正确。
 *   2. IsAbstractData：验证 TinyPbStruct 可通过基类指针使用。
 *   3. FieldAssignment：写入全部字段后读回一致。
 *   4. StatusFlagsInherited：通过基类引用修改 m_encodeSucc / m_decodeSucc，
 *      派生对象同步可见。
 */

#include "net/tinypb/tinypbdata.h"

#include <gtest/gtest.h>

#include <cstdint>
#include <memory>
#include <string>

// ─────────────────────────────────────────────────────────────────────────────
// Test 1：默认值验证
// ─────────────────────────────────────────────────────────────────────────────
TEST(TinyPbDataTest, DefaultValues)
{
    tinyrpc::TinyPbStruct s;

    // 数值字段默认值
    EXPECT_EQ(s.m_pkLen, 0);
    EXPECT_EQ(s.m_msgReqLen, 0);
    EXPECT_EQ(s.m_serviceNameLen, 0);
    EXPECT_EQ(s.m_errCode, 0);
    EXPECT_EQ(s.m_errInfoLen, 0);
    EXPECT_EQ(s.m_checkNum, -1);

    // 字符串字段默认为空
    EXPECT_TRUE(s.m_msgReq.empty());
    EXPECT_TRUE(s.m_serviceFullName.empty());
    EXPECT_TRUE(s.m_errInfo.empty());
    EXPECT_TRUE(s.m_pbData.empty());

    // 基类状态标记默认为 false
    EXPECT_FALSE(s.m_encodeSucc);
    EXPECT_FALSE(s.m_decodeSucc);
}

// ─────────────────────────────────────────────────────────────────────────────
// Test 2：可通过 AbstractData 基类指针使用
// ─────────────────────────────────────────────────────────────────────────────
TEST(TinyPbDataTest, IsAbstractData)
{
    // 裸指针
    tinyrpc::TinyPbStruct obj;
    tinyrpc::AbstractData *base = &obj;
    base->m_encodeSucc = true;
    EXPECT_TRUE(obj.m_encodeSucc);

    // shared_ptr
    auto sp = std::make_shared<tinyrpc::TinyPbStruct>();
    std::shared_ptr<tinyrpc::AbstractData> baseSp = sp;
    EXPECT_NE(baseSp.get(), nullptr);
    baseSp->m_decodeSucc = true;
    EXPECT_TRUE(sp->m_decodeSucc);
}

// ─────────────────────────────────────────────────────────────────────────────
// Test 3：字段赋值与读回
// ─────────────────────────────────────────────────────────────────────────────
TEST(TinyPbDataTest, FieldAssignment)
{
    tinyrpc::TinyPbStruct s;

    s.m_pkLen = 128;
    s.m_msgReq = "req-001";
    s.m_msgReqLen = static_cast<int32_t>(s.m_msgReq.size());
    s.m_serviceFullName = "QueryService.query_name";
    s.m_serviceNameLen = static_cast<int32_t>(s.m_serviceFullName.size());
    s.m_errCode = 42;
    s.m_errInfo = "service not found";
    s.m_errInfoLen = static_cast<int32_t>(s.m_errInfo.size());
    s.m_pbData = "\x08\x96\x01";
    s.m_checkNum = 0xDEAD;

    EXPECT_EQ(s.m_pkLen, 128);
    EXPECT_EQ(s.m_msgReq, "req-001");
    EXPECT_EQ(s.m_msgReqLen, 7);
    EXPECT_EQ(s.m_serviceFullName, "QueryService.query_name");
    EXPECT_EQ(s.m_serviceNameLen, 23);
    EXPECT_EQ(s.m_errCode, 42);
    EXPECT_EQ(s.m_errInfo, "service not found");
    EXPECT_EQ(s.m_errInfoLen, 17);
    EXPECT_EQ(s.m_pbData, std::string("\x08\x96\x01", 3));
    EXPECT_EQ(s.m_checkNum, 0xDEAD);
}

// ─────────────────────────────────────────────────────────────────────────────
// Test 4：通过基类引用修改状态标记，派生对象同步可见
// ─────────────────────────────────────────────────────────────────────────────
TEST(TinyPbDataTest, StatusFlagsInherited)
{
    tinyrpc::TinyPbStruct s;
    tinyrpc::AbstractData &ref = s;

    // 初始状态
    EXPECT_FALSE(ref.m_encodeSucc);
    EXPECT_FALSE(ref.m_decodeSucc);

    // 通过基类引用修改
    ref.m_encodeSucc = true;
    ref.m_decodeSucc = true;

    // 派生对象可见
    EXPECT_TRUE(s.m_encodeSucc);
    EXPECT_TRUE(s.m_decodeSucc);
}

// ─────────────────────────────────────────────────────────────────────────────
// main
// ─────────────────────────────────────────────────────────────────────────────
int main(int argc, char **argv)
{
    ::testing::InitGoogleTest(&argc, argv);
    int result = RUN_ALL_TESTS();
    if (result == 0) {
        std::cout << "[tinypb_data] PASS" << std::endl;
    }
    return result;
}
