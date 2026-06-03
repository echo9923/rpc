#include "comm/log.h"
#include "comm/runtime.h"
#include "net/http/httpdispatcher.h"
#include "net/http/httprequest.h"
#include "net/http/httpresponse.h"
#include "net/reactor.h"
#include "net/tcpconnection.h"
#include "net/tinypb/tinypbcodec.h"
#include "net/tinypb/tinypbdispatcher.h"
#include "test_tinypb_server.pb.h"

#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <future>
#include <iostream>
#include <memory>
#include <string>
#include <thread>

namespace {

class ContextServiceImpl : public QueryService {
 public:
    void query_name(
        google::protobuf::RpcController * /*controller*/,
        const queryNameReq *request,
        queryNameRes *response,
        google::protobuf::Closure *done) override
    {
        auto& context = tinyrpc::getRuntime().getCurrentRequestContext();
        m_observedReqId = context.getReqId();
        m_observedInterface = context.getInterfaceName();
        m_observedMethod = context.getMethodName();
        m_observedLocalAddr = context.getLocalAddr();
        m_observedPeerAddr = context.getPeerAddr();
        m_observedProtocol = context.getProtocolType();

        response->set_ret_code(0);
        response->set_res_info("runtime ok");
        response->set_req_no(request->req_no());
        response->set_id(request->id());
        response->set_name("RuntimeAlice");

        if (done != nullptr) {
            done->Run();
        }
    }

    std::string m_observedReqId;
    std::string m_observedInterface;
    std::string m_observedMethod;
    std::string m_observedLocalAddr;
    std::string m_observedPeerAddr;
    tinyrpc::ProtocolType m_observedProtocol {tinyrpc::ProtocolType::TinyPb};
};

class ContextHttpServlet : public tinyrpc::HttpServlet {
 public:
    void handle(tinyrpc::HttpRequest * /*request*/, tinyrpc::HttpResponse *response) override
    {
        auto& context = tinyrpc::getRuntime().getCurrentRequestContext();
        m_observedReqId = context.getReqId();
        m_observedInterface = context.getInterfaceName();
        m_observedMethod = context.getMethodName();
        m_observedLocalAddr = context.getLocalAddr();
        m_observedPeerAddr = context.getPeerAddr();
        m_observedProtocol = context.getProtocolType();

        response->setStatusCode(tinyrpc::HttpStatusCode::OK);
        response->setBody("runtime http ok");
    }

    std::string m_observedReqId;
    std::string m_observedInterface;
    std::string m_observedMethod;
    std::string m_observedLocalAddr;
    std::string m_observedPeerAddr;
    tinyrpc::ProtocolType m_observedProtocol {tinyrpc::ProtocolType::TinyPb};
};

std::shared_ptr<tinyrpc::TcpConnection> makeConnection(
    tinyrpc::Reactor *reactor,
    tinyrpc::AbstractCodec::Ptr codec,
    tinyrpc::AbstractDispatcher::Ptr dispatcher)
{
    return std::make_shared<tinyrpc::TcpConnection>(-1, reactor, codec, dispatcher);
}

tinyrpc::TinyPbStruct makeRequest(const std::string& reqId)
{
    queryNameReq pbRequest;
    pbRequest.set_req_no(6701);
    pbRequest.set_id(100);
    pbRequest.set_type(1);

    tinyrpc::TinyPbStruct request;
    request.m_reqId = reqId;
    request.m_serviceFullName = "QueryService.query_name";
    EXPECT_TRUE(pbRequest.SerializeToString(&request.m_pbData));
    return request;
}

std::string readFile(const std::string& path)
{
    std::ifstream input(path);
    return std::string(
        (std::istreambuf_iterator<char>(input)),
        std::istreambuf_iterator<char>()
    );
}

}  // namespace

TEST(RuntimeTest, DispatcherSetsTinyPbFullRequestContext)
{
    auto codec = std::make_shared<tinyrpc::TinyPbCodec>();
    auto dispatcher = std::make_shared<tinyrpc::TinyPbDispatcher>();
    auto service = std::make_shared<ContextServiceImpl>();
    ASSERT_TRUE(dispatcher->registerService(service));

    tinyrpc::Reactor reactor;
    auto conn = makeConnection(&reactor, codec, dispatcher);
    tinyrpc::TinyPbStruct request = makeRequest("runtime-req-001");

    dispatcher->dispatch(&request, conn.get());

    EXPECT_EQ(service->m_observedReqId, "runtime-req-001");
    EXPECT_EQ(service->m_observedInterface, "QueryService");
    EXPECT_EQ(service->m_observedMethod, "query_name");
    EXPECT_EQ(service->m_observedProtocol, tinyrpc::ProtocolType::TinyPb);
    EXPECT_FALSE(service->m_observedLocalAddr.empty());
    EXPECT_FALSE(service->m_observedPeerAddr.empty());
    EXPECT_TRUE(tinyrpc::getRuntime().getCurrentRequestContext().getReqId().empty());
    EXPECT_TRUE(tinyrpc::getRuntime().getCurrentRequestContext().getInterfaceName().empty());
    EXPECT_TRUE(tinyrpc::getRuntime().getCurrentRequestContext().getMethodName().empty());
}

TEST(RuntimeTest, HttpDispatcherSetsRequestContext)
{
    auto dispatcher = std::make_shared<tinyrpc::HttpDispatcher>();
    auto servlet = std::make_shared<ContextHttpServlet>();
    ASSERT_TRUE(dispatcher->registerServlet("/runtime", servlet));

    tinyrpc::HttpRequest request;
    tinyrpc::HttpResponse response;
    request.setPath("/runtime");
    request.setHeader("X-Req-Id", "http-req-001");

    dispatcher->dispatch(&request, &response);

    EXPECT_EQ(response.getStatusCodeValue(), 200);
    EXPECT_EQ(servlet->m_observedReqId, "http-req-001");
    EXPECT_EQ(servlet->m_observedInterface, "http");
    EXPECT_EQ(servlet->m_observedMethod, "/runtime");
    EXPECT_EQ(servlet->m_observedProtocol, tinyrpc::ProtocolType::Http);
    EXPECT_FALSE(servlet->m_observedLocalAddr.empty());
    EXPECT_FALSE(servlet->m_observedPeerAddr.empty());
}

TEST(RuntimeTest, RequestContextIsClearedAfterHttpAndTinyPb)
{
    auto httpDispatcher = std::make_shared<tinyrpc::HttpDispatcher>();
    auto servlet = std::make_shared<ContextHttpServlet>();
    ASSERT_TRUE(httpDispatcher->registerServlet("/runtime-clear", servlet));

    tinyrpc::HttpRequest httpRequest;
    tinyrpc::HttpResponse httpResponse;
    httpRequest.setPath("/runtime-clear");
    httpRequest.setHeader("X-Req-Id", "http-clear-req");
    httpDispatcher->dispatch(&httpRequest, &httpResponse);

    EXPECT_TRUE(tinyrpc::getRuntime().getCurrentRequestContext().getReqId().empty());
    EXPECT_TRUE(tinyrpc::getRuntime().getCurrentRequestContext().getInterfaceName().empty());
    EXPECT_TRUE(tinyrpc::getRuntime().getCurrentRequestContext().getMethodName().empty());

    auto codec = std::make_shared<tinyrpc::TinyPbCodec>();
    auto tinyPbDispatcher = std::make_shared<tinyrpc::TinyPbDispatcher>();
    auto service = std::make_shared<ContextServiceImpl>();
    ASSERT_TRUE(tinyPbDispatcher->registerService(service));
    tinyrpc::Reactor reactor;
    auto conn = makeConnection(&reactor, codec, tinyPbDispatcher);
    tinyrpc::TinyPbStruct request = makeRequest("runtime-clear-req");
    tinyPbDispatcher->dispatch(&request, conn.get());

    EXPECT_TRUE(tinyrpc::getRuntime().getCurrentRequestContext().getReqId().empty());
    EXPECT_TRUE(tinyrpc::getRuntime().getCurrentRequestContext().getInterfaceName().empty());
    EXPECT_TRUE(tinyrpc::getRuntime().getCurrentRequestContext().getMethodName().empty());
    EXPECT_TRUE(tinyrpc::getRuntime().getCurrentRequestContext().getLocalAddr().empty());
    EXPECT_TRUE(tinyrpc::getRuntime().getCurrentRequestContext().getPeerAddr().empty());
}

TEST(RuntimeTest, RequestContextIsThreadLocal)
{
    auto task = [](const std::string& reqId) {
        tinyrpc::getRuntime().setCurrentRequestContext(
            reqId,
            "QueryService",
            "query_name",
            "127.0.0.1:1000",
            "127.0.0.1:2000",
            tinyrpc::ProtocolType::TinyPb
        );
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
        std::string observed = tinyrpc::getRuntime().getCurrentRequestContext().getReqId();
        tinyrpc::getRuntime().clearCurrentRequestContext();
        return observed;
    };

    auto first = std::async(std::launch::async, task, "thread-req-1");
    auto second = std::async(std::launch::async, task, "thread-req-2");

    EXPECT_EQ(first.get(), "thread-req-1");
    EXPECT_EQ(second.get(), "thread-req-2");
    EXPECT_TRUE(tinyrpc::getRuntime().getCurrentRequestContext().getReqId().empty());
}

TEST(RuntimeTest, LoggerUsesCurrentContextReqId)
{
    std::filesystem::create_directories("build/log-tests");
    std::string path = "build/log-tests/runtime-context.log";
    std::filesystem::remove(path);

    ASSERT_TRUE(tinyrpc::Logger::init(path, tinyrpc::LogLevel::Debug));
    tinyrpc::getRuntime().setCurrentRequestContext(
        "log-context-req",
        "QueryService",
        "query_name",
        "local",
        "peer",
        tinyrpc::ProtocolType::TinyPb
    );

    InfoLog("log from runtime context");
    tinyrpc::Logger::flush();

    tinyrpc::getRuntime().clearCurrentRequestContext();
    tinyrpc::Logger::shutdown();

    std::string content = readFile(path);
    EXPECT_NE(content.find("[RPC]"), std::string::npos);
    EXPECT_NE(content.find("[reqId=log-context-req]"), std::string::npos);
    EXPECT_NE(content.find("[func="), std::string::npos);
    EXPECT_NE(content.find("log from runtime context"), std::string::npos);
    std::filesystem::remove(path);
}

int main(int argc, char **argv)
{
    ::testing::InitGoogleTest(&argc, argv);
    int result = RUN_ALL_TESTS();
    if (result == 0) {
        std::cout << "[runtime] PASS" << std::endl;
    }
    return result;
}
