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
#include <atomic>
#include <chrono>
#include <cerrno>
#include <cstring>
#include <sys/socket.h>
#include <thread>
#include <unistd.h>

#include <functional>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

namespace {

class FlagClosure : public google::protobuf::Closure {
 public:
    explicit FlagClosure(std::atomic<bool> *called)
        : m_called(called)
    {
    }

    void Run() override
    {
        m_called->store(true);
    }

 private:
    std::atomic<bool> *m_called {nullptr};
};

class CountClosure : public google::protobuf::Closure {
 public:
    explicit CountClosure(std::atomic<int> *runCount)
        : m_runCount(runCount)
    {
    }

    void Run() override
    {
        m_runCount->fetch_add(1);
    }

 private:
    std::atomic<int> *m_runCount {nullptr};
};

class ThreadRecordingClosure : public google::protobuf::Closure {
 public:
    explicit ThreadRecordingClosure(std::atomic<bool> *called, std::thread::id *threadId)
        : m_called(called),
          m_threadId(threadId)
    {
    }

    void Run() override
    {
        *m_threadId = std::this_thread::get_id();
        m_called->store(true);
    }

 private:
    std::atomic<bool> *m_called {nullptr};
    std::thread::id *m_threadId {nullptr};
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

bool waitUntil(const std::function<bool()>& pred, int timeoutMs)
{
    auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeoutMs);
    while (std::chrono::steady_clock::now() < deadline) {
        if (pred()) {
            return true;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    return pred();
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
        ASSERT_EQ(listen(m_listenFd, 16), 0) << std::strerror(errno);

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
        response.m_reqId = decodedRequest.m_reqId;
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
    channel.setReqIdGenerator([]() { return "async-req-001"; });
    QueryService_Stub stub(&channel);

    queryNameReq request;
    request.set_req_no(31);
    request.set_id(701);
    request.set_type(1);

    queryNameRes response;
    tinyrpc::TinyPbRpcController controller;
    std::atomic<bool> doneCalled {false};
    std::thread::id doneThreadId;
    ThreadRecordingClosure done(&doneCalled, &doneThreadId);

    stub.query_name(&controller, &request, &response, &done);
    serverThread.join();

    ASSERT_TRUE(serverOk) << serverError;
    ASSERT_TRUE(waitUntil([&]() { return doneCalled.load(); }, 1000));
    EXPECT_FALSE(controller.Failed()) << controller.ErrorText();
    EXPECT_EQ(decodedRequest.m_reqId, "async-req-001");
    EXPECT_EQ(decodedRequest.m_serviceFullName, "QueryService.query_name");
    EXPECT_EQ(response.name(), "async Alice");
    EXPECT_EQ(doneThreadId, channel.getIOThreadId());

    auto context = channel.getLastContext();
    ASSERT_NE(context, nullptr);
    EXPECT_EQ(context->m_reqId, "async-req-001");
    EXPECT_EQ(context->m_methodFullName, "QueryService.query_name");
    EXPECT_EQ(context->m_controller, &controller);
    EXPECT_EQ(context->m_request, &request);
    EXPECT_EQ(context->m_response, &response);
    EXPECT_EQ(context->m_done, &done);
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
    channel.setReqIdGenerator([]() { return "async-network-fail"; });
    QueryService_Stub stub(&channel);

    queryNameReq request;
    request.set_req_no(32);
    request.set_id(702);
    request.set_type(1);

    queryNameRes response;
    tinyrpc::TinyPbRpcController controller;
    std::atomic<bool> doneCalled {false};
    FlagClosure done(&doneCalled);

    stub.query_name(&controller, &request, &response, &done);
    closeIfValid(&nonListenFd);

    ASSERT_TRUE(waitUntil([&]() { return doneCalled.load(); }, 1000));
    EXPECT_TRUE(controller.Failed());
    EXPECT_EQ(controller.getErrorCode(), tinyrpc::ERROR_TCP_CONNECT_FAILED);

    auto context = channel.getLastContext();
    ASSERT_NE(context, nullptr);
    EXPECT_EQ(context->m_reqId, "async-network-fail");
    EXPECT_EQ(context->m_controller, &controller);
}

TEST_F(TinyPbRpcAsyncChannelTest, InvalidArgumentRunsDoneAndSetsError)
{
    tinyrpc::TinyPbRpcAsyncChannel channel(tinyrpc::IPAddress("127.0.0.1", getListenPort()));
    tinyrpc::TinyPbRpcController controller;
    std::atomic<bool> doneCalled {false};
    FlagClosure done(&doneCalled);

    channel.CallMethod(nullptr, &controller, nullptr, nullptr, &done);

    EXPECT_TRUE(doneCalled.load());
    EXPECT_TRUE(controller.Failed());
    EXPECT_EQ(controller.getErrorCode(), tinyrpc::ERROR_RPC_CHANNEL_INVALID_ARGUMENT);

    auto context = channel.getLastContext();
    ASSERT_NE(context, nullptr);
    EXPECT_EQ(context->m_controller, &controller);
    EXPECT_EQ(context->m_request, nullptr);
    EXPECT_EQ(context->m_response, nullptr);
    EXPECT_EQ(context->m_done, &done);
}

TEST_F(TinyPbRpcAsyncChannelTest, TenAsyncRequestsAllCompleteOnIOThread)
{
    constexpr int kRequestCount = 10;
    std::atomic<int> serverHandled {0};
    std::string serverError;

    std::thread serverThread([&]() {
        for (int i = 0; i < kRequestCount; ++i) {
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
            pbRes.set_name("async-" + std::to_string(pbReq.id()));

            tinyrpc::TinyPbStruct response;
            response.m_reqId = decodedRequest.m_reqId;
            response.m_serviceFullName = decodedRequest.m_serviceFullName;
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

            if (!writeAllToFd(clientFd, frame.data(), frame.size(), &serverError)) {
                closeIfValid(&clientFd);
                return;
            }
            closeIfValid(&clientFd);
            serverHandled.fetch_add(1);
        }
    });

    tinyrpc::TinyPbRpcAsyncChannel channel(tinyrpc::IPAddress("127.0.0.1", getListenPort()));
    int nextReqId = 0;
    channel.setReqIdGenerator([&]() {
        return "async-iothread-" + std::to_string(nextReqId++);
    });
    QueryService_Stub stub(&channel);

    std::vector<queryNameReq> requests(kRequestCount);
    std::vector<queryNameRes> responses(kRequestCount);
    std::vector<tinyrpc::TinyPbRpcController> controllers(kRequestCount);
    std::vector<std::atomic<bool>> doneFlags(kRequestCount);
    std::vector<std::thread::id> doneThreadIds(kRequestCount);
    std::vector<std::unique_ptr<ThreadRecordingClosure>> closures;
    closures.reserve(kRequestCount);

    for (int i = 0; i < kRequestCount; ++i) {
        requests[i].set_req_no(400 + i);
        requests[i].set_id(1100 + i);
        requests[i].set_type(1);
        closures.push_back(std::make_unique<ThreadRecordingClosure>(&doneFlags[i], &doneThreadIds[i]));
        stub.query_name(&controllers[i], &requests[i], &responses[i], closures.back().get());
    }

    ASSERT_TRUE(waitUntil([&]() {
        for (int i = 0; i < kRequestCount; ++i) {
            if (!doneFlags[i].load()) {
                return false;
            }
        }
        return true;
    }, 3000));

    serverThread.join();
    ASSERT_EQ(serverHandled.load(), kRequestCount) << serverError;
    EXPECT_EQ(channel.getPendingCount(), 0u);

    for (int i = 0; i < kRequestCount; ++i) {
        EXPECT_FALSE(controllers[i].Failed()) << controllers[i].ErrorText();
        EXPECT_EQ(responses[i].name(), "async-" + std::to_string(1100 + i));
        EXPECT_EQ(doneThreadIds[i], channel.getIOThreadId());
    }
}

TEST_F(TinyPbRpcAsyncChannelTest, PendingMapMatchesOutOfOrderResponses)
{
    tinyrpc::TinyPbRpcAsyncChannel channel(tinyrpc::IPAddress("127.0.0.1", getListenPort()));
    channel.setSyncFallbackEnabled(false);
    std::vector<std::string> reqIds {"async-pending-1", "async-pending-2"};
    size_t nextReqId = 0;
    channel.setReqIdGenerator([&]() {
        return reqIds[nextReqId++];
    });
    QueryService_Stub stub(&channel);

    queryNameReq request1;
    request1.set_req_no(101);
    request1.set_id(801);
    request1.set_type(1);

    queryNameReq request2;
    request2.set_req_no(102);
    request2.set_id(802);
    request2.set_type(1);

    queryNameRes response1;
    queryNameRes response2;
    tinyrpc::TinyPbRpcController controller1;
    tinyrpc::TinyPbRpcController controller2;
    std::atomic<bool> done1Called {false};
    std::atomic<bool> done2Called {false};
    FlagClosure done1(&done1Called);
    FlagClosure done2(&done2Called);

    stub.query_name(&controller1, &request1, &response1, &done1);
    stub.query_name(&controller2, &request2, &response2, &done2);

    EXPECT_EQ(channel.getPendingCount(), 2u);
    EXPECT_TRUE(channel.hasPending("async-pending-1"));
    EXPECT_TRUE(channel.hasPending("async-pending-2"));
    EXPECT_FALSE(done1Called.load());
    EXPECT_FALSE(done2Called.load());

    queryNameRes pbResponse2;
    pbResponse2.set_ret_code(0);
    pbResponse2.set_res_info("ok");
    pbResponse2.set_req_no(102);
    pbResponse2.set_id(802);
    pbResponse2.set_name("second");

    tinyrpc::TinyPbStruct tinyResponse2;
    tinyResponse2.m_reqId = "async-pending-2";
    tinyResponse2.m_serviceFullName = "QueryService.query_name";
    ASSERT_TRUE(pbResponse2.SerializeToString(&tinyResponse2.m_pbData));

    EXPECT_TRUE(channel.handleTinyPbResponse(tinyResponse2));
    EXPECT_TRUE(done2Called.load());
    EXPECT_FALSE(done1Called.load());
    EXPECT_EQ(response2.name(), "second");
    EXPECT_EQ(channel.getPendingCount(), 1u);
    EXPECT_FALSE(channel.hasPending("async-pending-2"));
    EXPECT_TRUE(channel.hasPending("async-pending-1"));

    queryNameRes pbResponse1;
    pbResponse1.set_ret_code(0);
    pbResponse1.set_res_info("ok");
    pbResponse1.set_req_no(101);
    pbResponse1.set_id(801);
    pbResponse1.set_name("first");

    tinyrpc::TinyPbStruct tinyResponse1;
    tinyResponse1.m_reqId = "async-pending-1";
    tinyResponse1.m_serviceFullName = "QueryService.query_name";
    ASSERT_TRUE(pbResponse1.SerializeToString(&tinyResponse1.m_pbData));

    EXPECT_TRUE(channel.handleTinyPbResponse(tinyResponse1));
    EXPECT_TRUE(done1Called.load());
    EXPECT_EQ(response1.name(), "first");
    EXPECT_EQ(channel.getPendingCount(), 0u);
    EXPECT_FALSE(controller1.Failed()) << controller1.ErrorText();
    EXPECT_FALSE(controller2.Failed()) << controller2.ErrorText();
}

TEST_F(TinyPbRpcAsyncChannelTest, UnknownReqIdResponseIsIgnored)
{
    tinyrpc::TinyPbRpcAsyncChannel channel(tinyrpc::IPAddress("127.0.0.1", getListenPort()));
    channel.setSyncFallbackEnabled(false);
    channel.setReqIdGenerator([]() { return "known-pending"; });
    QueryService_Stub stub(&channel);

    queryNameReq request;
    request.set_req_no(201);
    request.set_id(901);
    request.set_type(1);

    queryNameRes response;
    tinyrpc::TinyPbRpcController controller;
    std::atomic<bool> doneCalled {false};
    FlagClosure done(&doneCalled);

    stub.query_name(&controller, &request, &response, &done);
    ASSERT_EQ(channel.getPendingCount(), 1u);

    tinyrpc::TinyPbStruct unknownResponse;
    unknownResponse.m_reqId = "unknown-msg-req";
    unknownResponse.m_serviceFullName = "QueryService.query_name";

    EXPECT_FALSE(channel.handleTinyPbResponse(unknownResponse));
    EXPECT_EQ(channel.getPendingCount(), 1u);
    EXPECT_FALSE(doneCalled.load());
    EXPECT_TRUE(channel.hasPending("known-pending"));
}

TEST_F(TinyPbRpcAsyncChannelTest, PendingResponseErrorRunsClosure)
{
    tinyrpc::TinyPbRpcAsyncChannel channel(tinyrpc::IPAddress("127.0.0.1", getListenPort()));
    channel.setSyncFallbackEnabled(false);
    channel.setReqIdGenerator([]() { return "pending-error"; });
    QueryService_Stub stub(&channel);

    queryNameReq request;
    request.set_req_no(301);
    request.set_id(1001);
    request.set_type(1);

    queryNameRes response;
    tinyrpc::TinyPbRpcController controller;
    std::atomic<bool> doneCalled {false};
    FlagClosure done(&doneCalled);

    stub.query_name(&controller, &request, &response, &done);
    ASSERT_EQ(channel.getPendingCount(), 1u);

    tinyrpc::TinyPbStruct tinyResponse;
    tinyResponse.m_reqId = "pending-error";
    tinyResponse.m_serviceFullName = "QueryService.query_name";
    tinyResponse.m_errCode = tinyrpc::ERROR_SERVICE_NOT_FOUND;
    tinyResponse.m_errInfo = "service missing";

    EXPECT_TRUE(channel.handleTinyPbResponse(tinyResponse));
    EXPECT_TRUE(doneCalled.load());
    EXPECT_TRUE(controller.Failed());
    EXPECT_EQ(controller.getErrorCode(), tinyrpc::ERROR_SERVICE_NOT_FOUND);
    EXPECT_EQ(controller.ErrorText(), "service missing");
    EXPECT_EQ(channel.getPendingCount(), 0u);
}

TEST_F(TinyPbRpcAsyncChannelTest, PendingRequestTimeoutRunsClosureAndClearsPending)
{
    tinyrpc::TinyPbRpcAsyncChannel channel(tinyrpc::IPAddress("127.0.0.1", getListenPort()));
    channel.setSyncFallbackEnabled(false);
    channel.setReqIdGenerator([]() { return "pending-timeout"; });
    QueryService_Stub stub(&channel);

    queryNameReq request;
    request.set_req_no(501);
    request.set_id(1201);
    request.set_type(1);

    queryNameRes response;
    tinyrpc::TinyPbRpcController controller;
    controller.setTimeout(30);
    std::atomic<int> doneCount {0};
    CountClosure done(&doneCount);

    stub.query_name(&controller, &request, &response, &done);

    ASSERT_TRUE(waitUntil([&]() { return doneCount.load() == 1; }, 1000));
    EXPECT_TRUE(controller.Failed());
    EXPECT_EQ(controller.getErrorCode(), tinyrpc::ERROR_RPC_ASYNC_TIMEOUT);
    EXPECT_EQ(channel.getPendingCount(), 0u);
    EXPECT_FALSE(channel.hasPending("pending-timeout"));
}

TEST_F(TinyPbRpcAsyncChannelTest, LateResponseAfterTimeoutDoesNotRunClosureAgain)
{
    tinyrpc::TinyPbRpcAsyncChannel channel(tinyrpc::IPAddress("127.0.0.1", getListenPort()));
    channel.setSyncFallbackEnabled(false);
    channel.setReqIdGenerator([]() { return "late-after-timeout"; });
    QueryService_Stub stub(&channel);

    queryNameReq request;
    request.set_req_no(502);
    request.set_id(1202);
    request.set_type(1);

    queryNameRes response;
    tinyrpc::TinyPbRpcController controller;
    controller.setTimeout(30);
    std::atomic<int> doneCount {0};
    CountClosure done(&doneCount);

    stub.query_name(&controller, &request, &response, &done);
    ASSERT_TRUE(waitUntil([&]() { return doneCount.load() == 1; }, 1000));

    queryNameRes pbResponse;
    pbResponse.set_ret_code(0);
    pbResponse.set_res_info("ok");
    pbResponse.set_req_no(502);
    pbResponse.set_id(1202);
    pbResponse.set_name("late");

    tinyrpc::TinyPbStruct tinyResponse;
    tinyResponse.m_reqId = "late-after-timeout";
    tinyResponse.m_serviceFullName = "QueryService.query_name";
    ASSERT_TRUE(pbResponse.SerializeToString(&tinyResponse.m_pbData));

    EXPECT_FALSE(channel.handleTinyPbResponse(tinyResponse));
    EXPECT_EQ(doneCount.load(), 1);
    EXPECT_TRUE(controller.Failed());
    EXPECT_EQ(controller.getErrorCode(), tinyrpc::ERROR_RPC_ASYNC_TIMEOUT);
}

TEST_F(TinyPbRpcAsyncChannelTest, ControllerCancelClearsPendingAndRunsClosureOnce)
{
    tinyrpc::TinyPbRpcAsyncChannel channel(tinyrpc::IPAddress("127.0.0.1", getListenPort()));
    channel.setSyncFallbackEnabled(false);
    channel.setReqIdGenerator([]() { return "pending-cancel"; });
    QueryService_Stub stub(&channel);

    queryNameReq request;
    request.set_req_no(503);
    request.set_id(1203);
    request.set_type(1);

    queryNameRes response;
    tinyrpc::TinyPbRpcController controller;
    std::atomic<int> doneCount {0};
    CountClosure done(&doneCount);

    stub.query_name(&controller, &request, &response, &done);
    ASSERT_EQ(channel.getPendingCount(), 1u);

    controller.StartCancel();

    EXPECT_EQ(doneCount.load(), 1);
    EXPECT_TRUE(controller.IsCanceled());
    EXPECT_TRUE(controller.Failed());
    EXPECT_EQ(controller.getErrorCode(), tinyrpc::ERROR_RPC_ASYNC_CANCELED);
    EXPECT_EQ(channel.getPendingCount(), 0u);

    tinyrpc::TinyPbStruct tinyResponse;
    tinyResponse.m_reqId = "pending-cancel";
    tinyResponse.m_serviceFullName = "QueryService.query_name";
    EXPECT_FALSE(channel.handleTinyPbResponse(tinyResponse));
    EXPECT_EQ(doneCount.load(), 1);
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
