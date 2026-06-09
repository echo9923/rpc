#include "net/http/httprequest.h"
#include "net/http/httpresponse.h"
#include "net/http/httpdispatcher.h"
#include "net/http/httpservlet.h"

#include <gtest/gtest.h>

#include <stdexcept>
#include <iostream>
#include <memory>

class HelloServlet : public tinyrpc::HttpServlet {
 public:
    bool handle(tinyrpc::HttpRequest *request, tinyrpc::HttpResponse *response) override
    {
        response->setStatusCode(tinyrpc::HttpStatusCode::OK);
        response->setHeader("Content-Type", "text/plain");
        response->setBody("hello " + request->getPath());
        return true;
    }
};

class RootServlet : public tinyrpc::HttpServlet {
 public:
    bool handle(tinyrpc::HttpRequest *request, tinyrpc::HttpResponse *response) override
    {
        (void)request;
        response->setStatusCode(tinyrpc::HttpStatusCode::OK);
        response->setBody("root");
        return true;
    }
};

class FailServlet : public tinyrpc::HttpServlet {
 public:
    bool handle(tinyrpc::HttpRequest *request, tinyrpc::HttpResponse *response) override
    {
        (void)request;
        (void)response;
        return false;
    }
};

class ThrowServlet : public tinyrpc::HttpServlet {
 public:
    bool handle(tinyrpc::HttpRequest *request, tinyrpc::HttpResponse *response) override
    {
        (void)request;
        (void)response;
        throw std::runtime_error("servlet failed");
    }
};

TEST(HttpDispatcherTest, RegisteredPathReturnsBusinessBody)
{
    tinyrpc::HttpDispatcher dispatcher;
    ASSERT_TRUE(dispatcher.registerServlet("/hello", std::make_shared<HelloServlet>()));

    tinyrpc::HttpRequest request;
    request.setMethod(tinyrpc::HttpMethod::GET);
    request.setPath("/hello");

    tinyrpc::HttpResponse response;
    dispatcher.dispatch(&request, &response);

    EXPECT_EQ(response.getStatusCodeValue(), 200);
    EXPECT_EQ(response.getHeader("Content-Type"), "text/plain");
    EXPECT_EQ(response.getBody(), "hello /hello");
}

TEST(HttpDispatcherTest, RootPathReturnsRootServlet)
{
    tinyrpc::HttpDispatcher dispatcher;
    ASSERT_TRUE(dispatcher.registerServlet("/", std::make_shared<RootServlet>()));

    tinyrpc::HttpRequest request;
    request.setMethod(tinyrpc::HttpMethod::GET);
    request.setPath("/");

    tinyrpc::HttpResponse response;
    dispatcher.dispatch(&request, &response);

    EXPECT_EQ(response.getStatusCodeValue(), 200);
    EXPECT_EQ(response.getBody(), "root");
}

TEST(HttpDispatcherTest, UnknownPathReturnsNotFound)
{
    tinyrpc::HttpDispatcher dispatcher;

    tinyrpc::HttpRequest request;
    request.setMethod(tinyrpc::HttpMethod::GET);
    request.setPath("/missing");

    tinyrpc::HttpResponse response;
    dispatcher.dispatch(&request, &response);

    EXPECT_EQ(response.getStatusCode(), tinyrpc::HttpStatusCode::NotFound);
    EXPECT_EQ(response.getHeader("Content-Type"), "text/plain; charset=utf-8");
    EXPECT_EQ(response.getBody(), "Not Found");
}

TEST(HttpDispatcherTest, DuplicatePathRegistrationFails)
{
    tinyrpc::HttpDispatcher dispatcher;

    EXPECT_TRUE(dispatcher.registerServlet("/hello", std::make_shared<HelloServlet>()));
    EXPECT_FALSE(dispatcher.registerServlet("/hello", std::make_shared<HelloServlet>()));
}

TEST(HttpDispatcherTest, DuplicatePathKeepsOldServlet)
{
    tinyrpc::HttpDispatcher dispatcher;
    ASSERT_TRUE(dispatcher.registerServlet("/hello", std::make_shared<HelloServlet>()));
    EXPECT_FALSE(dispatcher.registerServlet("/hello", std::make_shared<RootServlet>()));

    tinyrpc::HttpRequest request;
    request.setPath("/hello");

    tinyrpc::HttpResponse response;
    dispatcher.dispatch(&request, &response);

    EXPECT_EQ(response.getStatusCodeValue(), 200);
    EXPECT_EQ(response.getBody(), "hello /hello");
}

TEST(HttpDispatcherTest, ServletReturnFalseGeneratesInternalServerError)
{
    tinyrpc::HttpDispatcher dispatcher;
    ASSERT_TRUE(dispatcher.registerServlet("/fail", std::make_shared<FailServlet>()));

    tinyrpc::HttpRequest request;
    request.setPath("/fail");

    tinyrpc::HttpResponse response;
    dispatcher.dispatch(&request, &response);

    EXPECT_EQ(response.getStatusCode(), tinyrpc::HttpStatusCode::InternalServerError);
    EXPECT_EQ(response.getBody(), "Internal Server Error");
}

TEST(HttpDispatcherTest, ServletThrowGeneratesInternalServerError)
{
    tinyrpc::HttpDispatcher dispatcher;
    ASSERT_TRUE(dispatcher.registerServlet("/throw", std::make_shared<ThrowServlet>()));

    tinyrpc::HttpRequest request;
    request.setPath("/throw");

    tinyrpc::HttpResponse response;
    dispatcher.dispatch(&request, &response);

    EXPECT_EQ(response.getStatusCode(), tinyrpc::HttpStatusCode::InternalServerError);
    EXPECT_EQ(response.getBody(), "Internal Server Error");
}

int main(int argc, char **argv)
{
    ::testing::InitGoogleTest(&argc, argv);
    int result = RUN_ALL_TESTS();
    if (result == 0) {
        std::cout << "[http_dispatcher] PASS" << std::endl;
    }
    return result;
}
