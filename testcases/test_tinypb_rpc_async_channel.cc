/*
 * test_tinypb_rpc_async_channel.cc -- 任务七十四：异步 Channel 生命周期外壳测试。
 */

#include "comm/errorcode.h"
#include "net/tcpbuffer.h"
#include "net/tinypb/tinypbcodec.h"
#include "net/tinypb/tinypbdata.h"
#include "net/tinypb/tinypbrpcasyncchannel.h"
#include "net/tinypb/tinypbrpccontroller.h"
#include "test_tinypb_server.pb.h"

#include <gtest/gtest.h>

#include <arpa/inet.h>
#include <cerrno>
#include <cstring>
#include <sys/socket.h>
#include <thread>
#include <unistd.h>

#include <iostream>
#include <string>

namespace {

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

        // read(2) 参数依次为：socket fd、接收缓冲区、最大读取字节数。
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
        // write(2) 参数依次为：socket fd、待写缓冲区、待写字节数。
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

}

class TinyPbRpcAsyncChannelTest : public ::testing::Test {
 protected:
    void SetUp() override
    {
        // socket(2) 参数依次为：地址族、socket 类型、协议号。
        // AF_INET + SOCK_STREAM + 0 创建 TCP socket。
        m_listenFd = socket(AF_INET, SOCK_STREAM, 0);
        ASSERT_GE(m_listenFd, 0) << std::strerror(errno);

        sockaddr_in addr {};
        addr.sin_family = AF_INET;
        addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        addr.sin_port = htons(0);

        ASSERT_EQ(bind(m_listenFd, reinterpret_cast<sockaddr *>(&addr), sizeof(addr)), 0)
            << std::strerror(errno);
        ASSERT_EQ(listen(m_listenFd, 1), 0) << std::strerror(errno);

        socklen_t len = sizeof(m_listenAddr);
        ASSERT_EQ(getsockname(m_listenFd, reinterpret_cast<sockaddr *>(&m_listenAddr), &len), 0)
            << std::strerror(errno);
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

TEST_F(TinyPbRpcAsyncChannelTest, StubCallTriggersDoneAndKeepsContextObservable)
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

        queryNameReq pbReq;
        if (!pbReq.ParseFromString(decodedRequest.m_pbData)) {
            serverError = "request parse failed";
            closeIfValid(&clientFd);
            return;
        }

        queryNameRes pbRes;
        pbRes.set_ret_code(0);
        pbRes.set_res_info("ok");
        pbRes.set_req_no(pbReq.req_no());
        pbRes.set_id(pbReq.id());
        pbRes.set_name("async Alice");

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

    tinyrpc::TinyPbRpcAsyncChannel channel(tinyrpc::IPAddress("127.0.0.1", getListenPort()));
    channel.setMsgReqGenerator([]() { return "async-req-001"; });
    QueryService_Stub stub(&channel);

    queryNameReq request;
    request.set_req_no(31);
    request.set_id(701);
    request.set_type(1);

    queryNameRes response;
    tinyrpc::TinyPbRpcController controller;
    bool doneCalled = false;
    FlagClosure done(&doneCalled);

    stub.query_name(&controller, &request, &response, &done);
    serverThread.join();

    ASSERT_TRUE(serverOk) << serverError;
    EXPECT_TRUE(doneCalled);
    EXPECT_FALSE(controller.Failed()) << controller.ErrorText();
    EXPECT_EQ(decodedRequest.m_msgReq, "async-req-001");
    EXPECT_EQ(decodedRequest.m_serviceFullName, "QueryService.query_name");
    EXPECT_EQ(response.name(), "async Alice");

    auto context = channel.getLastContext();
    ASSERT_NE(context, nullptr);
    EXPECT_EQ(context->msgReq, "async-req-001");
    EXPECT_EQ(context->methodFullName, "QueryService.query_name");
    EXPECT_EQ(context->controller, &controller);
    EXPECT_EQ(context->request, &request);
    EXPECT_EQ(context->response, &response);
    EXPECT_EQ(context->done, &done);
}

TEST_F(TinyPbRpcAsyncChannelTest, NetworkFailureStillRunsDone)
{
    int nonListenFd = socket(AF_INET, SOCK_STREAM, 0);
    ASSERT_GE(nonListenFd, 0);

    sockaddr_in nonListenAddr {};
    nonListenAddr.sin_family = AF_INET;
    nonListenAddr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    nonListenAddr.sin_port = htons(0);

    ASSERT_EQ(bind(nonListenFd, reinterpret_cast<sockaddr *>(&nonListenAddr), sizeof(nonListenAddr)), 0)
        << std::strerror(errno);

    socklen_t len = sizeof(nonListenAddr);
    ASSERT_EQ(getsockname(nonListenFd, reinterpret_cast<sockaddr *>(&nonListenAddr), &len), 0)
        << std::strerror(errno);

    tinyrpc::TinyPbRpcAsyncChannel channel(
        tinyrpc::IPAddress("127.0.0.1", ntohs(nonListenAddr.sin_port)));
    channel.setMsgReqGenerator([]() { return "async-network-fail"; });
    QueryService_Stub stub(&channel);

    queryNameReq request;
    request.set_req_no(32);
    request.set_id(702);
    request.set_type(1);

    queryNameRes response;
    tinyrpc::TinyPbRpcController controller;
    bool doneCalled = false;
    FlagClosure done(&doneCalled);

    stub.query_name(&controller, &request, &response, &done);
    closeIfValid(&nonListenFd);

    EXPECT_TRUE(doneCalled);
    EXPECT_TRUE(controller.Failed());
    EXPECT_EQ(controller.ErrorCode(), tinyrpc::ERROR_TCP_CONNECT_FAILED);

    auto context = channel.getLastContext();
    ASSERT_NE(context, nullptr);
    EXPECT_EQ(context->msgReq, "async-network-fail");
    EXPECT_EQ(context->controller, &controller);
}

TEST_F(TinyPbRpcAsyncChannelTest, InvalidArgumentRunsDoneAndSetsError)
{
    tinyrpc::TinyPbRpcAsyncChannel channel(tinyrpc::IPAddress("127.0.0.1", getListenPort()));
    tinyrpc::TinyPbRpcController controller;
    bool doneCalled = false;
    FlagClosure done(&doneCalled);

    channel.CallMethod(nullptr, &controller, nullptr, nullptr, &done);

    EXPECT_TRUE(doneCalled);
    EXPECT_TRUE(controller.Failed());
    EXPECT_EQ(controller.ErrorCode(), tinyrpc::ERROR_RPC_CHANNEL_INVALID_ARGUMENT);

    auto context = channel.getLastContext();
    ASSERT_NE(context, nullptr);
    EXPECT_EQ(context->controller, &controller);
    EXPECT_EQ(context->request, nullptr);
    EXPECT_EQ(context->response, nullptr);
    EXPECT_EQ(context->done, &done);
}

int main(int argc, char **argv)
{
    ::testing::InitGoogleTest(&argc, argv);
    int result = RUN_ALL_TESTS();
    if (result == 0) {
        std::cout << "[tinypb_rpc_async_channel] PASS" << std::endl;
    }
    return result;
}
