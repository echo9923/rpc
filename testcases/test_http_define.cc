#include "net/http/httpdefine.h"
#include "net/http/httprequest.h"
#include "net/http/httpresponse.h"

#include <gtest/gtest.h>

#include <iostream>
#include <string>

TEST(HttpDefineTest, StatusCodeToString)
{
    EXPECT_EQ(tinyrpc::httpCodeToString(200), "OK");
    EXPECT_EQ(tinyrpc::httpCodeToString(tinyrpc::HttpStatusCode::NotFound), "Not Found");
    EXPECT_EQ(tinyrpc::httpCodeToString(599), "Unknown");
}

TEST(HttpDefineTest, RequestHeaderCanSetAndRead)
{
    tinyrpc::HttpRequest request;
    request.setMethod(tinyrpc::HttpMethod::POST);
    request.setPath("/hello");
    request.setVersion("HTTP/1.1");
    request.setHeader("Host", "127.0.0.1");
    request.setHeader("Content-Type", "text/plain");
    request.setBody("hello");

    EXPECT_EQ(request.getMethod(), tinyrpc::HttpMethod::POST);
    EXPECT_EQ(request.getPath(), "/hello");
    EXPECT_EQ(request.getVersion(), "HTTP/1.1");
    EXPECT_EQ(request.getHeader("Host"), "127.0.0.1");
    EXPECT_EQ(request.getHeader("Content-Type"), "text/plain");
    EXPECT_EQ(request.getHeader("Missing"), "");
    EXPECT_TRUE(request.hasHeader("Host"));
    EXPECT_FALSE(request.hasHeader("Missing"));
    EXPECT_EQ(request.getBody(), "hello");
}

TEST(HttpDefineTest, ResponseGeneratesStatusLineHeadersAndBody)
{
    tinyrpc::HttpResponse response;
    response.setVersion("HTTP/1.1");
    response.setStatusCode(tinyrpc::HttpStatusCode::OK);
    response.setHeader("Content-Type", "text/plain");
    response.setBody("hello http");

    std::string raw = response.toString();

    EXPECT_NE(raw.find("HTTP/1.1 200 OK\r\n"), std::string::npos);
    EXPECT_NE(raw.find("Content-Type: text/plain\r\n"), std::string::npos);
    EXPECT_NE(raw.find("Content-Length: 10\r\n"), std::string::npos);
    EXPECT_NE(raw.find("\r\n\r\nhello http"), std::string::npos);
}

int main(int argc, char **argv)
{
    ::testing::InitGoogleTest(&argc, argv);
    int result = RUN_ALL_TESTS();
    if (result == 0) {
        std::cout << "[httpdefine] PASS" << std::endl;
    }
    return result;
}
