#include "net/http/http_request.h"
#include "net/http/httpcodec.h"
#include "net/tcpbuffer.h"

#include <gtest/gtest.h>

#include <iostream>
#include <string>

TEST(HttpCodecTest, DecodeGetRequest)
{
    tinyrpc::TcpBuffer buffer;
    buffer.append("GET /hello HTTP/1.1\r\nHost: 127.0.0.1\r\n\r\n");

    tinyrpc::HttpCodec codec;
    tinyrpc::HttpRequest request;
    codec.decode(&buffer, &request);

    EXPECT_TRUE(request.m_decodeSucc);
    EXPECT_EQ(request.getMethod(), tinyrpc::HttpMethod::GET);
    EXPECT_EQ(request.getPath(), "/hello");
    EXPECT_EQ(request.getVersion(), "HTTP/1.1");
    EXPECT_EQ(request.getHeader("Host"), "127.0.0.1");
    EXPECT_TRUE(request.getBody().empty());
    EXPECT_EQ(buffer.getReadableBytes(), 0u);
}

TEST(HttpCodecTest, DecodePostRequestWithContentLength)
{
    tinyrpc::TcpBuffer buffer;
    buffer.append("POST /submit HTTP/1.1\r\nHost: local\r\nContent-Length: 11\r\n\r\nhello world");

    tinyrpc::HttpCodec codec;
    tinyrpc::HttpRequest request;
    codec.decode(&buffer, &request);

    EXPECT_TRUE(request.m_decodeSucc);
    EXPECT_EQ(request.getMethod(), tinyrpc::HttpMethod::POST);
    EXPECT_EQ(request.getPath(), "/submit");
    EXPECT_EQ(request.getHeader("Content-Length"), "11");
    EXPECT_EQ(request.getBody(), "hello world");
    EXPECT_EQ(buffer.getReadableBytes(), 0u);
}

TEST(HttpCodecTest, DecodeHalfPacketKeepsBufferUntilComplete)
{
    tinyrpc::TcpBuffer buffer;
    buffer.append("POST /submit HTTP/1.1\r\nContent-Length: 11\r\n\r\nhello");

    tinyrpc::HttpCodec codec;
    tinyrpc::HttpRequest first;
    codec.decode(&buffer, &first);

    EXPECT_FALSE(first.m_decodeSucc);
    EXPECT_EQ(buffer.getReadableBytes(), 50u);

    buffer.append(" world");
    tinyrpc::HttpRequest second;
    codec.decode(&buffer, &second);

    EXPECT_TRUE(second.m_decodeSucc);
    EXPECT_EQ(second.getBody(), "hello world");
    EXPECT_EQ(buffer.getReadableBytes(), 0u);
}

TEST(HttpCodecTest, DecodeInvalidRequestLineFailsAndConsumesBadPacket)
{
    tinyrpc::TcpBuffer buffer;
    buffer.append("GET_ONLY_TWO_PARTS /bad\r\nHost: local\r\n\r\n");

    tinyrpc::HttpCodec codec;
    tinyrpc::HttpRequest request;
    codec.decode(&buffer, &request);

    EXPECT_FALSE(request.m_decodeSucc);
    EXPECT_EQ(buffer.getReadableBytes(), 0u);
}

int main(int argc, char **argv)
{
    ::testing::InitGoogleTest(&argc, argv);
    int result = RUN_ALL_TESTS();
    if (result == 0) {
        std::cout << "[http_codec] PASS" << std::endl;
    }
    return result;
}
