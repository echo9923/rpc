/*
 * test_tinypb_rpc_channel.cc -- 任务三十八：TinyPbRpcChannel 验收测试。
 *
 * 测试覆盖：
 *   1. Stub 通过 TinyPbRpcChannel 发起请求，mock server 能解出 serviceFullName 和 pbData。
 *   2. 服务端返回 TinyPB 错误码时，controller 记录框架层错误。
 *   3. 服务端返回非法 Protobuf payload 时，controller 记录反序列化错误。
 *   4. 成功和失败路径都会执行 done closure。
 */

#include "comm/errorcode.h"
#include "net/tcpbuffer.h"
#include "net/tinypb/tinypbcodec.h"
#include "net/tinypb/tinypbrpcchannel.h"
#include "net/tinypb/tinypbrpccontroller.h"
#include "test_tinypb_server.pb.h"

#include <gtest/gtest.h>

#include <arpa/inet.h>
#include <cerrno>
#include <cstring>
#include <sys/socket.h>
#include <thread>
#include <unistd.h>

#include <functional>
#include <string>

namespace {

void closeIfValid(int *fd)
{
    if (*fd >= 0) {
        close(*fd);
        *fd = -1;
    }
}

bool readTinyPbFromFd(int fd, tinyrpc::TinyPbStruct *pb, std::string *errorInfo)
{
    tinyrpc::TinyPbCodec codec;
    tinyrpc::TcpBuffer buffer(256);
    char data[1024];

    while (true) {
        codec.decode(&buffer, pb);
        if (pb->m_decodeSucc) {
            return true;
        }

        // read(2) 参数依次为：socket fd、接收缓冲区地址、最大读取字节数。
        ssize_t n = read(fd, data, sizeof(data));
        if (n > 0) {
            buffer.append(data, static_cast<size_t>(n));
            continue;
        }

        if (n == 0) {
            *errorInfo = "peer closed before TinyPB frame was complete";
            return false;
        }

        if (errno == EINTR) {
            continue;
        }

        *errorInfo = std::strerror(errno);
        return false;
    }
}

bool writeAllToFd(int fd, const char *data, size_t len, std::string *errorInfo)
{
    size_t written = 0;
    while (written < len) {
        // write(2) 参数依次为：socket fd、待写缓冲区地址、待写字节数。
        ssize_t n = write(fd, data + written, len - written);
        if (n > 0) {
            written += static_cast<size_t>(n);
            continue;
        }

        if (n == 0) {
            *errorInfo = "write returned zero";
            return false;
        }

        if (errno == EINTR) {
            continue;
        }

        *errorInfo = std::strerror(errno);
        return false;
    }
    return true;
}

bool encodeTinyPbToString(tinyrpc::TinyPbStruct *pb, std::string *frame)
{
    tinyrpc::TinyPbCodec codec;
    tinyrpc::TcpBuffer buffer(256);
    codec.encode(&buffer, pb);
    if (!pb->m_encodeSucc) {
        return false;
    }
    *frame = buffer.retrieveAllAsString();
    return true;
}

class FlagClosure : public google::protobuf::Closure {
 public:
    explicit FlagClosure(bool *called)
        : m_called(called)
    {
    }

    void Run() override
    {
        *m_called = true;
    }

 private:
    bool *m_called {nullptr};
};

} // namespace

class TinyPbRpcChannelTest : public ::testing::Test {
 protected:
    void SetUp() override
    {
        // AF_INET: IPv4；SOCK_STREAM: TCP 字节流；0 表示自动选择 TCP 协议。
        m_listenFd = socket(AF_INET, SOCK_STREAM, 0);
        ASSERT_GE(m_listenFd, 0) << "create listen socket failed: " << std::strerror(errno);

        sockaddr_in addr {};
        addr.sin_family = AF_INET;
        addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        addr.sin_port = htons(0);

        // bind(2) 将监听 socket 绑定到 127.0.0.1:0，让内核分配空闲端口。
        int rt = bind(m_listenFd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr));
        ASSERT_EQ(rt, 0) << "bind failed: " << std::strerror(errno);

        // listen(2) 把 socket 切换为监听状态，backlog=1 足够单连接 mock server 使用。
        rt = listen(m_listenFd, 1);
        ASSERT_EQ(rt, 0) << "listen failed: " << std::strerror(errno);

        socklen_t len = sizeof(m_listenAddr);
        rt = getsockname(m_listenFd, reinterpret_cast<sockaddr*>(&m_listenAddr), &len);
        ASSERT_EQ(rt, 0) << "getsockname failed: " << std::strerror(errno);
    }

    void TearDown() override
    {
        closeIfValid(&m_listenFd);
    }

    uint16_t getListenPort() const
    {
        return ntohs(m_listenAddr.sin_port);
    }

    int m_listenFd {-1};
    sockaddr_in m_listenAddr {};
};

TEST_F(TinyPbRpcChannelTest, StubCallSendsTinyPbRequestAndParsesResponse)
{
    tinyrpc::TinyPbStruct decodedRequest;
    bool serverOk = false;
    std::string serverError;

    std::thread serverThread([&]() {
        int clientFd = accept(m_listenFd, nullptr, nullptr);
        if (clientFd < 0) {
            serverError = std::strerror(errno);
            return;
        }

        if (!readTinyPbFromFd(clientFd, &decodedRequest, &serverError)) {
            closeIfValid(&clientFd);
            return;
        }

        queryNameReq serverReq;
        if (!serverReq.ParseFromString(decodedRequest.m_pbData)) {
            serverError = "request pbData parse failed";
            closeIfValid(&clientFd);
            return;
        }

        queryNameRes pbRes;
        pbRes.set_ret_code(0);
        pbRes.set_res_info("ok");
        pbRes.set_req_no(serverReq.req_no());
        pbRes.set_id(serverReq.id());
        pbRes.set_name("Alice");

        tinyrpc::TinyPbStruct response;
        response.m_msgReq = decodedRequest.m_msgReq;
        response.m_serviceFullName = decodedRequest.m_serviceFullName;
        response.m_errCode = 0;
        if (!pbRes.SerializeToString(&response.m_pbData)) {
            serverError = "response serialize failed";
            closeIfValid(&clientFd);
            return;
        }

        std::string frame;
        if (!encodeTinyPbToString(&response, &frame)) {
            serverError = "response encode failed";
            closeIfValid(&clientFd);
            return;
        }

        serverOk = writeAllToFd(clientFd, frame.data(), frame.size(), &serverError);
        closeIfValid(&clientFd);
    });

    tinyrpc::TinyPbRpcChannel channel(tinyrpc::IPAddress("127.0.0.1", getListenPort()));
    channel.setMsgReqGenerator([]() { return "channel-req-001"; });
    QueryService_Stub stub(&channel);

    queryNameReq request;
    request.set_req_no(7);
    request.set_id(100);
    request.set_type(1);

    queryNameRes response;
    tinyrpc::TinyPbRpcController controller;
    bool doneCalled = false;
    FlagClosure done(&doneCalled);

    stub.query_name(&controller, &request, &response, &done);
    serverThread.join();

    ASSERT_TRUE(serverOk) << serverError;
    ASSERT_FALSE(controller.Failed()) << controller.ErrorText();
    EXPECT_TRUE(doneCalled);
    EXPECT_EQ(decodedRequest.m_msgReq, "channel-req-001");
    EXPECT_EQ(decodedRequest.m_serviceFullName, "QueryService.query_name");
    EXPECT_EQ(response.ret_code(), 0);
    EXPECT_EQ(response.res_info(), "ok");
    EXPECT_EQ(response.req_no(), 7);
    EXPECT_EQ(response.id(), 100);
    EXPECT_EQ(response.name(), "Alice");
}

TEST_F(TinyPbRpcChannelTest, ControllerPresetMsgReqIsUsedByChannel)
{
    tinyrpc::TinyPbStruct decodedRequest;
    bool serverOk = false;
    std::string serverError;

    std::thread serverThread([&]() {
        int clientFd = accept(m_listenFd, nullptr, nullptr);
        if (clientFd < 0) {
            serverError = std::strerror(errno);
            return;
        }

        if (!readTinyPbFromFd(clientFd, &decodedRequest, &serverError)) {
            closeIfValid(&clientFd);
            return;
        }

        queryNameRes pbRes;
        pbRes.set_ret_code(0);
        pbRes.set_res_info("ok");
        pbRes.set_req_no(11);
        pbRes.set_id(104);
        pbRes.set_name("Bob");

        tinyrpc::TinyPbStruct response;
        response.m_msgReq = decodedRequest.m_msgReq;
        response.m_serviceFullName = decodedRequest.m_serviceFullName;
        ASSERT_TRUE(pbRes.SerializeToString(&response.m_pbData));

        std::string frame;
        if (!encodeTinyPbToString(&response, &frame)) {
            serverError = "response encode failed";
            closeIfValid(&clientFd);
            return;
        }

        serverOk = writeAllToFd(clientFd, frame.data(), frame.size(), &serverError);
        closeIfValid(&clientFd);
    });

    tinyrpc::TinyPbRpcChannel channel(tinyrpc::IPAddress("127.0.0.1", getListenPort()));
    QueryService_Stub stub(&channel);

    queryNameReq request;
    request.set_req_no(11);
    request.set_id(104);
    request.set_type(1);

    queryNameRes response;
    tinyrpc::TinyPbRpcController controller;
    controller.SetMsgReq("preset-msg-req");

    stub.query_name(&controller, &request, &response, nullptr);
    serverThread.join();

    ASSERT_TRUE(serverOk) << serverError;
    EXPECT_FALSE(controller.Failed()) << controller.ErrorText();
    EXPECT_EQ(controller.MsgReq(), "preset-msg-req");
    EXPECT_EQ(decodedRequest.m_msgReq, "preset-msg-req");
    EXPECT_EQ(response.name(), "Bob");
}

TEST_F(TinyPbRpcChannelTest, ServerTinyPbErrorSetsControllerError)
{
    bool serverOk = false;
    std::string serverError;

    std::thread serverThread([&]() {
        int clientFd = accept(m_listenFd, nullptr, nullptr);
        if (clientFd < 0) {
            serverError = std::strerror(errno);
            return;
        }

        tinyrpc::TinyPbStruct decodedRequest;
        if (!readTinyPbFromFd(clientFd, &decodedRequest, &serverError)) {
            closeIfValid(&clientFd);
            return;
        }

        tinyrpc::TinyPbStruct response;
        response.m_msgReq = decodedRequest.m_msgReq;
        response.m_serviceFullName = decodedRequest.m_serviceFullName;
        response.m_errCode = tinyrpc::ERROR_SERVICE_NOT_FOUND;
        response.m_errInfo = "service not found";

        std::string frame;
        if (!encodeTinyPbToString(&response, &frame)) {
            serverError = "response encode failed";
            closeIfValid(&clientFd);
            return;
        }

        serverOk = writeAllToFd(clientFd, frame.data(), frame.size(), &serverError);
        closeIfValid(&clientFd);
    });

    tinyrpc::TinyPbRpcChannel channel(tinyrpc::IPAddress("127.0.0.1", getListenPort()));
    channel.setMsgReqGenerator([]() { return "channel-req-002"; });
    QueryService_Stub stub(&channel);

    queryNameReq request;
    request.set_req_no(8);
    request.set_id(101);
    request.set_type(1);

    queryNameRes response;
    tinyrpc::TinyPbRpcController controller;
    bool doneCalled = false;
    FlagClosure done(&doneCalled);

    stub.query_name(&controller, &request, &response, &done);
    serverThread.join();

    ASSERT_TRUE(serverOk) << serverError;
    EXPECT_TRUE(doneCalled);
    EXPECT_TRUE(controller.Failed());
    EXPECT_EQ(controller.ErrorCode(), tinyrpc::ERROR_SERVICE_NOT_FOUND);
    EXPECT_EQ(controller.ErrorText(), "service not found");
}

TEST_F(TinyPbRpcChannelTest, BadResponsePayloadSetsDeserializeError)
{
    bool serverOk = false;
    std::string serverError;

    std::thread serverThread([&]() {
        int clientFd = accept(m_listenFd, nullptr, nullptr);
        if (clientFd < 0) {
            serverError = std::strerror(errno);
            return;
        }

        tinyrpc::TinyPbStruct decodedRequest;
        if (!readTinyPbFromFd(clientFd, &decodedRequest, &serverError)) {
            closeIfValid(&clientFd);
            return;
        }

        tinyrpc::TinyPbStruct response;
        response.m_msgReq = decodedRequest.m_msgReq;
        response.m_serviceFullName = decodedRequest.m_serviceFullName;
        response.m_errCode = 0;
        response.m_pbData = "not a queryNameRes protobuf payload";

        std::string frame;
        if (!encodeTinyPbToString(&response, &frame)) {
            serverError = "response encode failed";
            closeIfValid(&clientFd);
            return;
        }

        serverOk = writeAllToFd(clientFd, frame.data(), frame.size(), &serverError);
        closeIfValid(&clientFd);
    });

    tinyrpc::TinyPbRpcChannel channel(tinyrpc::IPAddress("127.0.0.1", getListenPort()));
    channel.setMsgReqGenerator([]() { return "channel-req-003"; });
    QueryService_Stub stub(&channel);

    queryNameReq request;
    request.set_req_no(9);
    request.set_id(102);
    request.set_type(1);

    queryNameRes response;
    tinyrpc::TinyPbRpcController controller;
    bool doneCalled = false;
    FlagClosure done(&doneCalled);

    stub.query_name(&controller, &request, &response, &done);
    serverThread.join();

    ASSERT_TRUE(serverOk) << serverError;
    EXPECT_TRUE(doneCalled);
    EXPECT_TRUE(controller.Failed());
    EXPECT_EQ(controller.ErrorCode(), tinyrpc::ERROR_FAILED_DESERIALIZE);
    EXPECT_FALSE(controller.ErrorText().empty());
}

TEST_F(TinyPbRpcChannelTest, NetworkFailureSetsControllerError)
{
    // AF_INET: IPv4；SOCK_STREAM: TCP；0 表示自动选择 TCP。
    // 这里只 bind 不 listen，让端口被占用但不可连接，稳定触发 connect() 失败。
    int nonListenFd = socket(AF_INET, SOCK_STREAM, 0);
    ASSERT_GE(nonListenFd, 0) << "create non-listen socket failed: " << std::strerror(errno);

    sockaddr_in nonListenAddr {};
    nonListenAddr.sin_family = AF_INET;
    nonListenAddr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    nonListenAddr.sin_port = htons(0);

    // bind(2) 参数依次为：socket fd、本地地址、地址长度。
    int rt = bind(nonListenFd, reinterpret_cast<sockaddr*>(&nonListenAddr), sizeof(nonListenAddr));
    ASSERT_EQ(rt, 0) << "bind non-listen socket failed: " << std::strerror(errno);

    socklen_t len = sizeof(nonListenAddr);
    rt = getsockname(nonListenFd, reinterpret_cast<sockaddr*>(&nonListenAddr), &len);
    ASSERT_EQ(rt, 0) << "getsockname non-listen socket failed: " << std::strerror(errno);

    tinyrpc::TinyPbRpcChannel channel(
        tinyrpc::IPAddress("127.0.0.1", ntohs(nonListenAddr.sin_port)));
    channel.setMsgReqGenerator([]() { return "channel-req-network-fail"; });
    QueryService_Stub stub(&channel);

    queryNameReq request;
    request.set_req_no(10);
    request.set_id(103);
    request.set_type(1);

    queryNameRes response;
    tinyrpc::TinyPbRpcController controller;
    bool doneCalled = false;
    FlagClosure done(&doneCalled);

    stub.query_name(&controller, &request, &response, &done);
    closeIfValid(&nonListenFd);

    EXPECT_TRUE(doneCalled);
    EXPECT_TRUE(controller.Failed());
    EXPECT_EQ(controller.ErrorCode(), tinyrpc::ERROR_RPC_CHANNEL_NETWORK);
    EXPECT_FALSE(controller.ErrorText().empty());
}

TEST_F(TinyPbRpcChannelTest, MismatchedResponseMsgReqSetsControllerError)
{
    bool serverOk = false;
    std::string serverError;

    std::thread serverThread([&]() {
        int clientFd = accept(m_listenFd, nullptr, nullptr);
        if (clientFd < 0) {
            serverError = std::strerror(errno);
            return;
        }

        tinyrpc::TinyPbStruct decodedRequest;
        if (!readTinyPbFromFd(clientFd, &decodedRequest, &serverError)) {
            closeIfValid(&clientFd);
            return;
        }

        queryNameRes pbRes;
        pbRes.set_ret_code(0);
        pbRes.set_res_info("ok");
        pbRes.set_req_no(12);
        pbRes.set_id(105);
        pbRes.set_name("Carol");

        tinyrpc::TinyPbStruct response;
        response.m_msgReq = "wrong-msg-req";
        response.m_serviceFullName = decodedRequest.m_serviceFullName;
        ASSERT_TRUE(pbRes.SerializeToString(&response.m_pbData));

        std::string frame;
        if (!encodeTinyPbToString(&response, &frame)) {
            serverError = "response encode failed";
            closeIfValid(&clientFd);
            return;
        }

        serverOk = writeAllToFd(clientFd, frame.data(), frame.size(), &serverError);
        closeIfValid(&clientFd);
    });

    tinyrpc::TinyPbRpcChannel channel(tinyrpc::IPAddress("127.0.0.1", getListenPort()));
    channel.setMsgReqGenerator([]() { return "expected-msg-req"; });
    QueryService_Stub stub(&channel);

    queryNameReq request;
    request.set_req_no(12);
    request.set_id(105);
    request.set_type(1);

    queryNameRes response;
    tinyrpc::TinyPbRpcController controller;

    stub.query_name(&controller, &request, &response, nullptr);
    serverThread.join();

    ASSERT_TRUE(serverOk) << serverError;
    EXPECT_TRUE(controller.Failed());
    EXPECT_EQ(controller.ErrorCode(), tinyrpc::ERROR_RPC_MSGREQ_MISMATCH);
    EXPECT_FALSE(controller.ErrorText().empty());
}

int main(int argc, char **argv)
{
    ::testing::InitGoogleTest(&argc, argv);
    int result = RUN_ALL_TESTS();
    if (result == 0) {
        std::cout << "[tinypb_rpc_channel] PASS" << std::endl;
    }
    return result;
}
