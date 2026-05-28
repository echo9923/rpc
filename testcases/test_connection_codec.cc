/*
 * test_connection_codec.cc — 任务三十二/三十三/三十五：连接层协议接入验收测试。
 *
 * 本测试验证 TcpConnection::execute() 中使用的 decode→encode 回环模式：
 *   1. CodecRoundTrip：encode 一帧，decode 后拿到与输入一致的字段，
 *      再 encode 回第二帧，两次编码的输出字节完全一致。
 *   2. PartialFrameNoConsume：半包时 decode 失败且不消费 buffer。
 *   3. SendProtocolDataWritesOutput：sendProtocolData() 将协议数据
 *      编码后写入 outputBuffer，可从中解码出原始字段。
 *   4. ExecuteDispatchesTinyPbRpcRequest：从 execute() 入口打通整条
 *      服务端 RPC 链路，验证 CallMethod 真正被调用。
 */

#include "net/reactor.h"
#include "net/tcpbuffer.h"
#include "net/tcpconnection.h"
#include "net/tinypb/tinypbcodec.h"
#include "net/tinypb/tinypbdispatcher.h"
#include "test_tinypb_server.pb.h"

#include <gtest/gtest.h>

#include <memory>
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
// Test 3：sendProtocolData() 将协议数据编码后写入 outputBuffer
// 验证 dispatcher 通过 sendProtocolData() 写回响应的路径可通。
// ─────────────────────────────────────────────────────────────────────────────
TEST(ConnectionCodecTest, SendProtocolDataWritesOutput)
{
    tinyrpc::TinyPbCodec codec;

    // 创建 Reactor 和 TcpConnection（fd=-1 不对应真实 socket，仅用于测试）
    tinyrpc::Reactor reactor;
    auto conn = std::make_shared<tinyrpc::TcpConnection>(-1, &reactor, std::make_shared<tinyrpc::TinyPbCodec>());

    // 构造请求
    tinyrpc::TinyPbStruct request;
    request.m_msgReq = "req-send-001";
    request.m_serviceFullName = "OrderService.create";
    request.m_pbData = "\xAB\xCD";
    request.m_errCode = 42;
    request.m_errInfo = "test error";

    // 调用 sendProtocolData()
    conn->sendProtocolData(&request);

    // 从 outputBuffer 解码响应
    tinyrpc::TcpBuffer *outputBuf = conn->getOutputBuffer();
    ASSERT_GT(outputBuf->getReadableBytes(), 0u);

    tinyrpc::TinyPbStruct decoded;
    codec.decode(outputBuf, &decoded);
    ASSERT_TRUE(decoded.m_decodeSucc);

    // 验证字段与请求一致
    EXPECT_EQ(decoded.m_msgReq, "req-send-001");
    EXPECT_EQ(decoded.m_serviceFullName, "OrderService.create");
    EXPECT_EQ(decoded.m_pbData, std::string("\xAB\xCD", 2));
    EXPECT_EQ(decoded.m_errCode, 42);
    EXPECT_EQ(decoded.m_errInfo, "test error");

    // outputBuffer 应被完全消费
    EXPECT_EQ(outputBuf->getReadableBytes(), 0u);
}

// ─────────────────────────────────────────────────────────────────────────────
// QueryServiceImpl：QueryService 的最小实现，用于全链路测试。
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
// Test 4：从 execute() 入口打通整条服务端 RPC 链路
// queryNameReq → SerializeToString → TinyPbStruct → encode → inputBuffer
// → execute() → decode → dispatch → CallMethod → encode → outputBuffer
// → decode → ParseFromString → queryNameRes
// ─────────────────────────────────────────────────────────────────────────────
TEST(ConnectionCodecTest, ExecuteDispatchesTinyPbRpcRequest)
{
    auto codec = std::make_shared<tinyrpc::TinyPbCodec>();
    auto dispatcher = std::make_shared<tinyrpc::TinyPbDispatcher>();

    // 注册服务
    auto service = std::make_shared<QueryServiceImpl>();
    dispatcher->registerService(service);

    tinyrpc::Reactor reactor;
    auto conn = std::make_shared<tinyrpc::TcpConnection>(-1, &reactor, codec, dispatcher);

    // 构造 Protobuf 请求消息并序列化
    queryNameReq pbReq;
    pbReq.set_req_no(7);
    pbReq.set_id(200);
    pbReq.set_type(3);
    std::string pbData;
    ASSERT_TRUE(pbReq.SerializeToString(&pbData));

    // 构造 TinyPB 请求帧
    tinyrpc::TinyPbStruct request;
    request.m_msgReq = "req-chain-001";
    request.m_serviceFullName = "QueryService.query_name";
    request.m_pbData = pbData;
    request.m_errCode = 0;
    request.m_errInfo = "";

    // encode 到完整帧并 append 到 inputBuffer
    tinyrpc::TcpBuffer encodeBuf(256);
    codec->encode(&encodeBuf, &request);
    ASSERT_TRUE(request.m_encodeSucc);
    conn->getInputBuffer()->append(encodeBuf.retrieveAllAsString());

    // 调用 execute()，驱动整条链路
    conn->execute();

    // 从 outputBuffer 解码 TinyPB 响应
    tinyrpc::TcpBuffer *outputBuf = conn->getOutputBuffer();
    ASSERT_GT(outputBuf->getReadableBytes(), 0u);

    tinyrpc::TinyPbStruct response;
    codec->decode(outputBuf, &response);
    ASSERT_TRUE(response.m_decodeSucc);

    EXPECT_EQ(response.m_msgReq, "req-chain-001");
    EXPECT_EQ(response.m_serviceFullName, "QueryService.query_name");
    EXPECT_EQ(response.m_errCode, 0);

    // 将 response.m_pbData 反序列化为 queryNameRes 并验证
    queryNameRes pbRes;
    ASSERT_TRUE(pbRes.ParseFromString(response.m_pbData));
    EXPECT_EQ(pbRes.ret_code(), 0);
    EXPECT_EQ(pbRes.res_info(), "ok");
    EXPECT_EQ(pbRes.req_no(), 7);
    EXPECT_EQ(pbRes.id(), 200);
    EXPECT_EQ(pbRes.name(), "Alice");
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
