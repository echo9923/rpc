/*
 * test_protobuf_service.cc — 任务三十四：Protobuf 示例服务验收测试。
 *
 * 测试覆盖：
 *   1. ServiceDescriptorName：QueryService descriptor 的 full_name 为 "QueryService"。
 *   2. FindMethodByName：能找到 query_name 方法。
 *   3. SerializeRequest：queryNameReq 能序列化。
 *   4. ResponseFieldRoundTrip：queryNameRes 能设置字段并读回。
 */

#include "test_tinypb_server.pb.h"

#include <gtest/gtest.h>

#include <string>

// ─────────────────────────────────────────────────────────────────────────────
// Test 1：QueryService descriptor 的 full_name 为 "QueryService"
// ─────────────────────────────────────────────────────────────────────────────
TEST(ProtobufServiceTest, ServiceDescriptorName)
{
    // GetDescriptor() 返回 ServiceDescriptor，full_name() 是服务的全限定名。
    // 由于 proto 文件中没有 package 声明，full_name 就是 "QueryService"。
    EXPECT_EQ(QueryService::descriptor()->full_name(), "QueryService");
}

// ─────────────────────────────────────────────────────────────────────────────
// Test 2：能找到 query_name 方法
// ─────────────────────────────────────────────────────────────────────────────
TEST(ProtobufServiceTest, FindMethodByName)
{
    // FindMethodByName 在 ServiceDescriptor 中按方法名查找 MethodDescriptor。
    auto *method = QueryService::descriptor()->FindMethodByName("query_name");
    ASSERT_NE(method, nullptr);

    // 验证方法的输入输出类型名
    EXPECT_EQ(method->input_type()->name(), "queryNameReq");
    EXPECT_EQ(method->output_type()->name(), "queryNameRes");
}

// ─────────────────────────────────────────────────────────────────────────────
// Test 3：queryNameReq 能序列化
// ─────────────────────────────────────────────────────────────────────────────
TEST(ProtobufServiceTest, SerializeRequest)
{
    queryNameReq req;
    req.set_req_no(42);
    req.set_id(100);
    req.set_type(1);

    // SerializeToString 将消息序列化为 protobuf 二进制格式。
    std::string data;
    EXPECT_TRUE(req.SerializeToString(&data));
    EXPECT_GT(data.size(), 0u);

    // 反序列化回来验证字段一致
    queryNameReq decoded;
    EXPECT_TRUE(decoded.ParseFromString(data));
    EXPECT_EQ(decoded.req_no(), 42);
    EXPECT_EQ(decoded.id(), 100);
    EXPECT_EQ(decoded.type(), 1);
}

// ─────────────────────────────────────────────────────────────────────────────
// Test 4：queryNameRes 能设置字段并读回
// ─────────────────────────────────────────────────────────────────────────────
TEST(ProtobufServiceTest, ResponseFieldRoundTrip)
{
    queryNameRes res;
    res.set_ret_code(0);
    res.set_res_info("ok");
    res.set_req_no(42);
    res.set_id(100);
    res.set_name("Alice");

    EXPECT_EQ(res.ret_code(), 0);
    EXPECT_EQ(res.res_info(), "ok");
    EXPECT_EQ(res.req_no(), 42);
    EXPECT_EQ(res.id(), 100);
    EXPECT_EQ(res.name(), "Alice");

    // 序列化再反序列化验证
    std::string data;
    EXPECT_TRUE(res.SerializeToString(&data));

    queryNameRes decoded;
    EXPECT_TRUE(decoded.ParseFromString(data));
    EXPECT_EQ(decoded.ret_code(), 0);
    EXPECT_EQ(decoded.res_info(), "ok");
    EXPECT_EQ(decoded.req_no(), 42);
    EXPECT_EQ(decoded.id(), 100);
    EXPECT_EQ(decoded.name(), "Alice");
}

// ─────────────────────────────────────────────────────────────────────────────
// main
// ─────────────────────────────────────────────────────────────────────────────
int main(int argc, char **argv)
{
    ::testing::InitGoogleTest(&argc, argv);
    int result = RUN_ALL_TESTS();
    if (result == 0) {
        std::cout << "[protobuf_service] PASS" << std::endl;
    }
    return result;
}
