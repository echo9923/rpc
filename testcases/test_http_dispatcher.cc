#include "net/http/http_request.h"
#include "net/http/http_response.h"
#include "net/http/httpdispatcher.h"
#include "net/http/httpservlet.h"

#include <gtest/gtest.h>

#include <iostream>
#include <memory>

class HelloServlet : public tinyrpc::HttpServlet {
 public:
    void handle(tinyrpc::HttpRequest *request, tinyrpc::HttpResponse *response) override
    {
        response->setStatusCode(tinyrpc::HttpStatusCode::OK);
        response->setHeader("Content-Type", "text/plain");
        response->setBody("hello " + request->getPath());
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

TEST(HttpDispatcherTest, UnknownPathReturnsNotFound)
{
    tinyrpc::HttpDispatcher dispatcher;

    tinyrpc::HttpRequest request;
    request.setMethod(tinyrpc::HttpMethod::GET);
    request.setPath("/missing");

    tinyrpc::HttpResponse response;
    dispatcher.dispatch(&request, &response);

    EXPECT_EQ(response.getStatusCode(), tinyrpc::HttpStatusCode::NotFound);
    EXPECT_EQ(response.getHeader("Content-Type"), "text/plain");
    EXPECT_EQ(response.getBody(), "404 Not Found");
}

TEST(HttpDispatcherTest, DuplicatePathRegistrationFails)
{
    tinyrpc::HttpDispatcher dispatcher;

    EXPECT_TRUE(dispatcher.registerServlet("/hello", std::make_shared<HelloServlet>()));
    EXPECT_FALSE(dispatcher.registerServlet("/hello", std::make_shared<HelloServlet>()));
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
