#include "comm/start.h"
#include "net/http/httprequest.h"
#include "net/http/httpresponse.h"
#include "net/http/httpdispatcher.h"
#include "net/timer.h"
#include "net/tinypb/tinypbdispatcher.h"
#include "test_tinypb_server.pb.h"

#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <iostream>
#include <memory>
#include <thread>

namespace {

class QueryServiceImpl : public QueryService {
 public:
    void query_name(
        google::protobuf::RpcController * /*controller*/,
        const queryNameReq *request,
        queryNameRes *response,
        google::protobuf::Closure *done) override
    {
        response->set_ret_code(0);
        response->set_res_info("start ok");
        response->set_req_no(request->req_no());
        response->set_id(request->id());
        response->set_name("StartAlice");

        if (done != nullptr) {
            done->Run();
        }
    }
};

class StartHelloServlet : public tinyrpc::HttpServlet {
 public:
    void handle(tinyrpc::HttpRequest *request, tinyrpc::HttpResponse *response) override
    {
        (void)request;
        response->setStatusCode(tinyrpc::HttpStatusCode::OK);
        response->setHeader("Content-Type", "text/plain");
        response->setBody("start hello");
    }
};

}  // namespace

TEST(StartTest, AddTimerTaskReturnsFalseBeforeServer)
{
    auto task = std::make_shared<tinyrpc::TimerTask>(1, false, []() {});

    EXPECT_FALSE(tinyrpc::AddTimerTask(task));
    EXPECT_FALSE(tinyrpc::AddTimerTask(nullptr));
}

TEST(StartTest, InitConfigLoadsXmlAndStartTinyPbServer)
{
    ASSERT_TRUE(tinyrpc::InitConfig("conf/test_start_tinypb.xml"));
    ASSERT_TRUE(tinyrpc::StartRpcServer());

    auto server = tinyrpc::GetServer();
    ASSERT_NE(server, nullptr);
    EXPECT_EQ(server->getLocalAddress().getIp(), "127.0.0.1");
    EXPECT_EQ(server->getLocalAddress().getPort(), 0);
    EXPECT_EQ(server->getIOThreadNum(), 0);
    EXPECT_TRUE(REGISTER_SERVICE(QueryServiceImpl));

    auto dispatcher = tinyrpc::GetTinyPbDispatcher();
    ASSERT_NE(dispatcher, nullptr);
    EXPECT_NE(dispatcher->findService("QueryService"), nullptr);
}

TEST(StartTest, GetConfigReturnsLoadedConfig)
{
    ASSERT_TRUE(tinyrpc::InitConfig("conf/test_tinypb_server.xml"));

    EXPECT_EQ(tinyrpc::GetConfig().getServerPort(), 24139);
    EXPECT_EQ(tinyrpc::GetConstConfig().getLogPrefix(), "tinypb_server");
    EXPECT_EQ(tinyrpc::GetConstConfig().getRpcLogLevel(), tinyrpc::LogLevel::Info);
}

TEST(StartTest, GetIOThreadPoolSizeUsesConfig)
{
    ASSERT_TRUE(tinyrpc::InitConfig("conf/test_tinypb_server.xml"));

    EXPECT_EQ(tinyrpc::GetIOThreadPoolSize(), 2);
}

TEST(StartTest, AddTimerTaskRunsOnServerReactor)
{
    ASSERT_TRUE(tinyrpc::InitConfig("conf/test_start_tinypb.xml"));
    ASSERT_TRUE(tinyrpc::StartRpcServer());

    std::atomic<int> runCount {0};
    auto task = std::make_shared<tinyrpc::TimerTask>(1, false, [&runCount]() {
        ++runCount;
    });
    ASSERT_TRUE(tinyrpc::AddTimerTask(task));

    auto server = tinyrpc::GetServer();
    ASSERT_NE(server, nullptr);
    for (int i = 0; i < 20 && runCount.load() == 0; ++i) {
        server->waitOnce(10);
    }

    EXPECT_EQ(runCount.load(), 1);
}

TEST(StartTest, InitConfigLoadsXmlAndStartHttpServer)
{
    ASSERT_TRUE(tinyrpc::InitConfig("conf/test_start_http.xml"));
    ASSERT_TRUE(tinyrpc::StartRpcServer());

    auto server = tinyrpc::GetServer();
    ASSERT_NE(server, nullptr);
    EXPECT_EQ(server->getLocalAddress().getIp(), "127.0.0.1");
    EXPECT_EQ(server->getLocalAddress().getPort(), 0);
    EXPECT_EQ(server->getIOThreadNum(), 0);
    EXPECT_TRUE(REGISTER_HTTP_SERVLET("/hello", StartHelloServlet));

    auto dispatcher = tinyrpc::GetHttpDispatcher();
    ASSERT_NE(dispatcher, nullptr);

    tinyrpc::HttpRequest request;
    tinyrpc::HttpResponse response;
    request.setPath("/hello");
    dispatcher->dispatch(&request, &response);

    EXPECT_EQ(response.getStatusCodeValue(), 200);
    EXPECT_EQ(response.getBody(), "start hello");
}

int main(int argc, char **argv)
{
    ::testing::InitGoogleTest(&argc, argv);
    int result = RUN_ALL_TESTS();
    if (result == 0) {
        std::cout << "[start] PASS" << std::endl;
    }
    return result;
}
