/*
 * test_tinypb_dispatcher.cc — 任务三十三/三十四/三十五：TinyPbDispatcher 验收测试。
 *
 * 测试覆盖：
 *   1. ParseServiceFullNameValid：合法 serviceFullName 正确拆分。
 *   2. ParseServiceFullNameRejectsInvalid：各种非法输入返回 false。
 *   3. RegisterServiceStoresByFullName：注册 QueryServiceImpl 后能按 full_name 找到。
 *   4. FindMethodReturnsDescriptor：注册后能找到 query_name 方法。
 *   5. DispatchCallsServiceAndSerializesResponse：dispatch 真正调用 CallMethod，
 *      响应 pbData 可反序列化为 queryNameRes，字段正确。
 *   6. DispatchRejectsUnknownService：未知服务返回 ERROR_SERVICE_NOT_FOUND。
 *   7. DispatchRejectsUnknownMethod：未知方法返回 ERROR_METHOD_NOT_FOUND。
 *   8. DispatchRejectsBadServiceFullName：非法 serviceFullName 返回 ERROR_PARSE_SERVICE_NAME。
 *   9. DispatchRejectsBadPbData：非法 pbData 返回 ERROR_FAILED_DESERIALIZE。
 */

#include "comm/errorcode.h"
#include "net/reactor.h"
#include "net/tcpconnection.h"
#include "net/tinypb/tinypbcodec.h"
#include "net/tinypb/tinypbdispatcher.h"
#include "test_tinypb_server.pb.h"

#include <gtest/gtest.h>

#include <memory>
#include <string>

// ─────────────────────────────────────────────────────────────────────────────
// QueryServiceImpl：QueryService 的最小实现，用于测试服务注册和 RPC 调用。
// 继承 protoc 生成的 QueryService 基类（cc_generic_services = true 时生成）。
// query_name 将请求中的 req_no 和 id 原样返回，name 固定为 "Alice"。
// ─────────────────────────────────────────────────────────────────────────────
class QueryServiceImpl : public QueryService {
 public:
    void query_name(
        google::protobuf::RpcController * /*controller*/,
        const queryNameReq *request,
        queryNameRes *response,
        google::protobuf::Closure *done) override
    {
        response->set_ret_code(0);
        response->set_res_info("ok");
        response->set_req_no(request->req_no());
        response->set_id(request->id());
        response->set_name("Alice");

        if (done != nullptr) {
            done->Run();
        }
    }
};

// ─────────────────────────────────────────────────────────────────────────────
// 辅助：创建带 codec 和 dispatcher 的 TcpConnection（fd=-1，不做真实 I/O）。
// ─────────────────────────────────────────────────────────────────────────────
static std::shared_ptr<tinyrpc::TcpConnection> makeConnection(
    tinyrpc::Reactor *reactor,
    tinyrpc::AbstractCodec::Ptr codec,
    tinyrpc::AbstractDispatcher::Ptr dispatcher)
{
    return std::make_shared<tinyrpc::TcpConnection>(-1, reactor, codec, dispatcher);
}

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
// Test 3：注册 QueryServiceImpl 后能按 full_name 找到
// ─────────────────────────────────────────────────────────────────────────────
TEST(TinyPbDispatcherTest, RegisterServiceStoresByFullName)
{
    tinyrpc::TinyPbDispatcher dispatcher;

    auto service = std::make_shared<QueryServiceImpl>();
    EXPECT_TRUE(dispatcher.registerService(service));

    // 按 full_name 查找，应能找到
    auto *found = dispatcher.findService("QueryService");
    EXPECT_NE(found, nullptr);

    // 重复注册应返回 false
    auto service2 = std::make_shared<QueryServiceImpl>();
    EXPECT_FALSE(dispatcher.registerService(service2));
}

// ─────────────────────────────────────────────────────────────────────────────
// Test 4：注册后能找到 query_name 方法
// ─────────────────────────────────────────────────────────────────────────────
TEST(TinyPbDispatcherTest, FindMethodReturnsDescriptor)
{
    tinyrpc::TinyPbDispatcher dispatcher;

    auto service = std::make_shared<QueryServiceImpl>();
    dispatcher.registerService(service);

    auto *svc = dispatcher.findService("QueryService");
    ASSERT_NE(svc, nullptr);

    // 能找到 query_name
    auto *method = dispatcher.findMethod(svc, "query_name");
    EXPECT_NE(method, nullptr);

    // 找不到不存在的方法
    auto *badMethod = dispatcher.findMethod(svc, "nonexistent");
    EXPECT_EQ(badMethod, nullptr);
}

// ─────────────────────────────────────────────────────────────────────────────
// Test 5：dispatch 真正调用 CallMethod，响应 pbData 可反序列化为 queryNameRes
// ─────────────────────────────────────────────────────────────────────────────
TEST(TinyPbDispatcherTest, DispatchCallsServiceAndSerializesResponse)
{
    auto codec = std::make_shared<tinyrpc::TinyPbCodec>();
    auto dispatcher = std::make_shared<tinyrpc::TinyPbDispatcher>();

    // 注册服务
    auto service = std::make_shared<QueryServiceImpl>();
    dispatcher->registerService(service);

    tinyrpc::Reactor reactor;
    auto conn = makeConnection(&reactor, codec, dispatcher);

    // 构造 Protobuf 请求消息并序列化
    queryNameReq pbReq;
    pbReq.set_req_no(42);
    pbReq.set_id(100);
    pbReq.set_type(1);
    std::string pbData;
    ASSERT_TRUE(pbReq.SerializeToString(&pbData));

    // 构造 TinyPB 请求
    tinyrpc::TinyPbStruct request;
    request.m_reqId = "req-dispatch-001";
    request.m_serviceFullName = "QueryService.query_name";
    request.m_pbData = pbData;
    request.m_errCode = 0;
    request.m_errInfo = "";

    // 调用 dispatch
    dispatcher->dispatch(&request, conn.get());

    // 从 outputBuffer 解码 TinyPB 响应
    tinyrpc::TcpBuffer *outputBuf = conn->getOutputBuffer();
    ASSERT_GT(outputBuf->getReadableBytes(), 0u);

    tinyrpc::TinyPbStruct response;
    codec->decode(outputBuf, &response);
    ASSERT_TRUE(response.m_decodeSucc);

    // 验证 TinyPB 层面字段
    EXPECT_EQ(response.m_reqId, "req-dispatch-001");
    EXPECT_EQ(response.m_serviceFullName, "QueryService.query_name");
    EXPECT_EQ(response.m_errCode, 0);
    EXPECT_EQ(response.m_errInfo, "");

    // 将 response.m_pbData 反序列化为 queryNameRes 并验证
    queryNameRes pbRes;
    ASSERT_TRUE(pbRes.ParseFromString(response.m_pbData));
    EXPECT_EQ(pbRes.ret_code(), 0);
    EXPECT_EQ(pbRes.res_info(), "ok");
    EXPECT_EQ(pbRes.req_no(), 42);
    EXPECT_EQ(pbRes.id(), 100);
    EXPECT_EQ(pbRes.name(), "Alice");

    // outputBuffer 应被完全消费
    EXPECT_EQ(outputBuf->getReadableBytes(), 0u);
}

// ─────────────────────────────────────────────────────────────────────────────
// Test 6：未知服务返回 ERROR_SERVICE_NOT_FOUND
// ─────────────────────────────────────────────────────────────────────────────
TEST(TinyPbDispatcherTest, DispatchRejectsUnknownService)
{
    auto codec = std::make_shared<tinyrpc::TinyPbCodec>();
    auto dispatcher = std::make_shared<tinyrpc::TinyPbDispatcher>();

    // 注册 QueryService，但请求的是 UnknownService
    auto service = std::make_shared<QueryServiceImpl>();
    dispatcher->registerService(service);

    tinyrpc::Reactor reactor;
    auto conn = makeConnection(&reactor, codec, dispatcher);

    tinyrpc::TinyPbStruct request;
    request.m_reqId = "req-unknown-svc";
    request.m_serviceFullName = "UnknownService.query_name";

    dispatcher->dispatch(&request, conn.get());

    tinyrpc::TcpBuffer *outputBuf = conn->getOutputBuffer();
    ASSERT_GT(outputBuf->getReadableBytes(), 0u);

    tinyrpc::TinyPbStruct response;
    codec->decode(outputBuf, &response);
    ASSERT_TRUE(response.m_decodeSucc);

    EXPECT_EQ(response.m_errCode, tinyrpc::ERROR_SERVICE_NOT_FOUND);
    EXPECT_FALSE(response.m_errInfo.empty());
}

// ─────────────────────────────────────────────────────────────────────────────
// Test 7：未知方法返回 ERROR_METHOD_NOT_FOUND
// ─────────────────────────────────────────────────────────────────────────────
TEST(TinyPbDispatcherTest, DispatchRejectsUnknownMethod)
{
    auto codec = std::make_shared<tinyrpc::TinyPbCodec>();
    auto dispatcher = std::make_shared<tinyrpc::TinyPbDispatcher>();

    auto service = std::make_shared<QueryServiceImpl>();
    dispatcher->registerService(service);

    tinyrpc::Reactor reactor;
    auto conn = makeConnection(&reactor, codec, dispatcher);

    tinyrpc::TinyPbStruct request;
    request.m_reqId = "req-unknown-method";
    request.m_serviceFullName = "QueryService.unknown_method";

    dispatcher->dispatch(&request, conn.get());

    tinyrpc::TcpBuffer *outputBuf = conn->getOutputBuffer();
    ASSERT_GT(outputBuf->getReadableBytes(), 0u);

    tinyrpc::TinyPbStruct response;
    codec->decode(outputBuf, &response);
    ASSERT_TRUE(response.m_decodeSucc);

    EXPECT_EQ(response.m_errCode, tinyrpc::ERROR_METHOD_NOT_FOUND);
    EXPECT_FALSE(response.m_errInfo.empty());
}

// ─────────────────────────────────────────────────────────────────────────────
// Test 8：非法 serviceFullName 返回 ERROR_PARSE_SERVICE_NAME
// ─────────────────────────────────────────────────────────────────────────────
TEST(TinyPbDispatcherTest, DispatchRejectsBadServiceFullName)
{
    auto codec = std::make_shared<tinyrpc::TinyPbCodec>();
    auto dispatcher = std::make_shared<tinyrpc::TinyPbDispatcher>();

    tinyrpc::Reactor reactor;
    auto conn = makeConnection(&reactor, codec, dispatcher);

    tinyrpc::TinyPbStruct request;
    request.m_reqId = "req-bad-name";
    request.m_serviceFullName = "BadServiceName";

    dispatcher->dispatch(&request, conn.get());

    tinyrpc::TcpBuffer *outputBuf = conn->getOutputBuffer();
    ASSERT_GT(outputBuf->getReadableBytes(), 0u);

    tinyrpc::TinyPbStruct response;
    codec->decode(outputBuf, &response);
    ASSERT_TRUE(response.m_decodeSucc);

    EXPECT_EQ(response.m_errCode, tinyrpc::ERROR_PARSE_SERVICE_NAME);
    EXPECT_FALSE(response.m_errInfo.empty());
}

// ─────────────────────────────────────────────────────────────────────────────
// Test 9：非法 pbData 返回 ERROR_FAILED_DESERIALIZE
// ─────────────────────────────────────────────────────────────────────────────
TEST(TinyPbDispatcherTest, DispatchRejectsBadPbData)
{
    auto codec = std::make_shared<tinyrpc::TinyPbCodec>();
    auto dispatcher = std::make_shared<tinyrpc::TinyPbDispatcher>();

    auto service = std::make_shared<QueryServiceImpl>();
    dispatcher->registerService(service);

    tinyrpc::Reactor reactor;
    auto conn = makeConnection(&reactor, codec, dispatcher);

    tinyrpc::TinyPbStruct request;
    request.m_reqId = "req-bad-pb";
    request.m_serviceFullName = "QueryService.query_name";
    request.m_pbData = "bad protobuf data";

    dispatcher->dispatch(&request, conn.get());

    tinyrpc::TcpBuffer *outputBuf = conn->getOutputBuffer();
    ASSERT_GT(outputBuf->getReadableBytes(), 0u);

    tinyrpc::TinyPbStruct response;
    codec->decode(outputBuf, &response);
    ASSERT_TRUE(response.m_decodeSucc);

    EXPECT_EQ(response.m_errCode, tinyrpc::ERROR_FAILED_DESERIALIZE);
    EXPECT_FALSE(response.m_errInfo.empty());
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
