#include "net/http/httprequest.h"
#include "net/http/httpresponse.h"
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

TEST(HttpCodecTest, DecodeOriginFormTargetWithQuery)
{
    tinyrpc::TcpBuffer buffer;
    buffer.append("GET /hello?name=alice&empty=&name=bob HTTP/1.1\r\nHost: local\r\n\r\n");

    tinyrpc::HttpCodec codec;
    tinyrpc::HttpRequest request;
    codec.decode(&buffer, &request);

    EXPECT_TRUE(request.m_decodeSucc);
    EXPECT_EQ(request.getRequestTarget(), "/hello?name=alice&empty=&name=bob");
    EXPECT_EQ(request.getPath(), "/hello");
    EXPECT_EQ(request.getQueryString(), "name=alice&empty=&name=bob");
    EXPECT_TRUE(request.hasQueryParam("name"));
    EXPECT_EQ(request.getQueryParam("name"), "bob");
    EXPECT_EQ(request.getQueryParam("empty"), "");
    EXPECT_FALSE(request.hasQueryParam("missing"));
    EXPECT_EQ(buffer.getReadableBytes(), 0u);
}

TEST(HttpCodecTest, DecodeAbsoluteFormTargetWithQuery)
{
    tinyrpc::TcpBuffer buffer;
    buffer.append("GET http://example.com:8080/api/search?q=rpc HTTP/1.0\r\nHost: example.com\r\n\r\n");

    tinyrpc::HttpCodec codec;
    tinyrpc::HttpRequest request;
    codec.decode(&buffer, &request);

    EXPECT_TRUE(request.m_decodeSucc);
    EXPECT_EQ(request.getRequestTarget(), "http://example.com:8080/api/search?q=rpc");
    EXPECT_EQ(request.getPath(), "/api/search");
    EXPECT_EQ(request.getQueryString(), "q=rpc");
    EXPECT_EQ(request.getQueryParam("q"), "rpc");
    EXPECT_EQ(request.getVersion(), "HTTP/1.0");
    EXPECT_EQ(buffer.getReadableBytes(), 0u);
}

TEST(HttpCodecTest, DecodeRootPathAndEmptyQuery)
{
    tinyrpc::TcpBuffer buffer;
    buffer.append("GET /? HTTP/1.1\r\nHost: local\r\n\r\n");

    tinyrpc::HttpCodec codec;
    tinyrpc::HttpRequest request;
    codec.decode(&buffer, &request);

    EXPECT_TRUE(request.m_decodeSucc);
    EXPECT_EQ(request.getPath(), "/");
    EXPECT_EQ(request.getQueryString(), "");
    EXPECT_TRUE(request.getQueryParams().empty());
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

TEST(HttpCodecTest, DecodeHeaderLookupIsCaseInsensitive)
{
    tinyrpc::TcpBuffer buffer;
    buffer.append("POST /submit HTTP/1.1\r\nHOST: local\r\ncontent-length: 5\r\n\r\nhello");

    tinyrpc::HttpCodec codec;
    tinyrpc::HttpRequest request;
    codec.decode(&buffer, &request);

    EXPECT_TRUE(request.m_decodeSucc);
    EXPECT_TRUE(request.hasHeader("host"));
    EXPECT_TRUE(request.hasHeader("HOST"));
    EXPECT_EQ(request.getHeader("Host"), "local");
    EXPECT_EQ(request.getHeader("Content-Length"), "5");
    EXPECT_EQ(request.getHeader("content-length"), "5");
    EXPECT_EQ(request.getBody(), "hello");
    EXPECT_EQ(buffer.getReadableBytes(), 0u);
}

TEST(HttpCodecTest, DecodeDuplicateHeaderKeepsLastValue)
{
    tinyrpc::TcpBuffer buffer;
    buffer.append("GET /hello HTTP/1.1\r\nHost: first\r\nhost: second\r\n\r\n");

    tinyrpc::HttpCodec codec;
    tinyrpc::HttpRequest request;
    codec.decode(&buffer, &request);

    EXPECT_TRUE(request.m_decodeSucc);
    EXPECT_EQ(request.getHeader("Host"), "second");
    EXPECT_EQ(buffer.getReadableBytes(), 0u);
}

TEST(HttpCodecTest, DecodePostWithoutBodySucceeds)
{
    tinyrpc::TcpBuffer buffer;
    buffer.append("POST /submit HTTP/1.1\r\nHost: local\r\n\r\n");

    tinyrpc::HttpCodec codec;
    tinyrpc::HttpRequest request;
    codec.decode(&buffer, &request);

    EXPECT_TRUE(request.m_decodeSucc);
    EXPECT_EQ(request.getMethod(), tinyrpc::HttpMethod::POST);
    EXPECT_TRUE(request.getBody().empty());
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

TEST(HttpCodecTest, DecodeInvalidContentLengthFailsAndConsumesBadPacket)
{
    tinyrpc::TcpBuffer buffer;
    buffer.append("POST /submit HTTP/1.1\r\nContent-Length: 1x\r\n\r\nhello");

    tinyrpc::HttpCodec codec;
    tinyrpc::HttpRequest request;
    codec.decode(&buffer, &request);

    EXPECT_FALSE(request.m_decodeSucc);
    EXPECT_EQ(buffer.getReadableBytes(), 5u);
}

TEST(HttpCodecTest, DecodeNegativeContentLengthFailsAndConsumesBadPacket)
{
    tinyrpc::TcpBuffer buffer;
    buffer.append("POST /submit HTTP/1.1\r\nContent-Length: -1\r\n\r\nhello");

    tinyrpc::HttpCodec codec;
    tinyrpc::HttpRequest request;
    codec.decode(&buffer, &request);

    EXPECT_FALSE(request.m_decodeSucc);
    EXPECT_EQ(buffer.getReadableBytes(), 5u);
}

TEST(HttpCodecTest, DecodeTooLargeContentLengthFailsAndConsumesBadPacket)
{
    tinyrpc::TcpBuffer buffer;
    buffer.append("POST /submit HTTP/1.1\r\nContent-Length: 1048577\r\n\r\nhello");

    tinyrpc::HttpCodec codec;
    tinyrpc::HttpRequest request;
    codec.decode(&buffer, &request);

    EXPECT_FALSE(request.m_decodeSucc);
    EXPECT_EQ(buffer.getReadableBytes(), 5u);
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

TEST(HttpCodecTest, DecodeInvalidMethodFailsAndConsumesBadPacket)
{
    tinyrpc::TcpBuffer buffer;
    buffer.append("PUT /hello HTTP/1.1\r\nHost: local\r\n\r\n");

    tinyrpc::HttpCodec codec;
    tinyrpc::HttpRequest request;
    codec.decode(&buffer, &request);

    EXPECT_FALSE(request.m_decodeSucc);
    EXPECT_EQ(buffer.getReadableBytes(), 0u);
}

TEST(HttpCodecTest, DecodeInvalidVersionFailsAndConsumesBadPacket)
{
    tinyrpc::TcpBuffer buffer;
    buffer.append("GET /hello HTTP/2.0\r\nHost: local\r\n\r\n");

    tinyrpc::HttpCodec codec;
    tinyrpc::HttpRequest request;
    codec.decode(&buffer, &request);

    EXPECT_FALSE(request.m_decodeSucc);
    EXPECT_EQ(buffer.getReadableBytes(), 0u);
}

TEST(HttpCodecTest, EncodeOkResponse)
{
    tinyrpc::HttpResponse response;
    response.setStatusCode(tinyrpc::HttpStatusCode::OK);
    response.setHeader("Content-Type", "text/plain");
    response.setBody("hello");

    tinyrpc::TcpBuffer buffer;
    tinyrpc::HttpCodec codec;
    codec.encode(&buffer, &response);

    EXPECT_TRUE(response.m_encodeSucc);
    std::string raw = buffer.retrieveAllAsString();
    EXPECT_NE(raw.find("HTTP/1.1 200 OK\r\n"), std::string::npos);
    EXPECT_NE(raw.find("content-type: text/plain\r\n"), std::string::npos);
    EXPECT_NE(raw.find("content-length: 5\r\n"), std::string::npos);
    EXPECT_NE(raw.find("connection: close\r\n"), std::string::npos);
    EXPECT_NE(raw.find("\r\n\r\nhello"), std::string::npos);
}

TEST(HttpCodecTest, EncodeOkResponseAddsDefaultHeaders)
{
    tinyrpc::HttpResponse response;
    response.setStatusCode(tinyrpc::HttpStatusCode::OK);
    response.setBody("hello");

    tinyrpc::TcpBuffer buffer;
    tinyrpc::HttpCodec codec;
    codec.encode(&buffer, &response);

    EXPECT_TRUE(response.m_encodeSucc);
    EXPECT_EQ(response.getHeader("Content-Length"), "5");
    EXPECT_EQ(response.getHeader("Content-Type"), "text/plain; charset=utf-8");
    EXPECT_EQ(response.getHeader("Connection"), "close");
    std::string raw = buffer.retrieveAllAsString();
    EXPECT_NE(raw.find("HTTP/1.1 200 OK\r\n"), std::string::npos);
    EXPECT_NE(raw.find("content-type: text/plain; charset=utf-8\r\n"), std::string::npos);
    EXPECT_NE(raw.find("content-length: 5\r\n"), std::string::npos);
    EXPECT_NE(raw.find("connection: close\r\n"), std::string::npos);
}

TEST(HttpCodecTest, EncodeNotFoundResponse)
{
    tinyrpc::HttpResponse response;
    response.setErrorResponse(tinyrpc::HttpStatusCode::NotFound);

    tinyrpc::TcpBuffer buffer;
    tinyrpc::HttpCodec codec;
    codec.encode(&buffer, &response);

    EXPECT_TRUE(response.m_encodeSucc);
    std::string raw = buffer.retrieveAllAsString();
    EXPECT_NE(raw.find("HTTP/1.1 404 Not Found\r\n"), std::string::npos);
    EXPECT_NE(raw.find("content-length: 9\r\n"), std::string::npos);
    EXPECT_NE(raw.find("connection: close\r\n"), std::string::npos);
    EXPECT_NE(raw.find("\r\n\r\nNot Found"), std::string::npos);
}

TEST(HttpCodecTest, EncodeBadRequestAndInternalServerError)
{
    tinyrpc::HttpCodec codec;

    tinyrpc::HttpResponse badRequest;
    badRequest.setErrorResponse(tinyrpc::HttpStatusCode::BadRequest);
    tinyrpc::TcpBuffer badRequestBuffer;
    codec.encode(&badRequestBuffer, &badRequest);

    EXPECT_TRUE(badRequest.m_encodeSucc);
    std::string badRequestRaw = badRequestBuffer.retrieveAllAsString();
    EXPECT_NE(badRequestRaw.find("HTTP/1.1 400 Bad Request\r\n"), std::string::npos);
    EXPECT_NE(badRequestRaw.find("\r\n\r\nBad Request"), std::string::npos);

    tinyrpc::HttpResponse internalError;
    internalError.setErrorResponse(tinyrpc::HttpStatusCode::InternalServerError);
    tinyrpc::TcpBuffer internalErrorBuffer;
    codec.encode(&internalErrorBuffer, &internalError);

    EXPECT_TRUE(internalError.m_encodeSucc);
    std::string internalErrorRaw = internalErrorBuffer.retrieveAllAsString();
    EXPECT_NE(internalErrorRaw.find("HTTP/1.1 500 Internal Server Error\r\n"), std::string::npos);
    EXPECT_NE(internalErrorRaw.find("\r\n\r\nInternal Server Error"), std::string::npos);
}

TEST(HttpCodecTest, EncodeResponseCorrectsContentLength)
{
    tinyrpc::HttpResponse response;
    response.setStatusCode(tinyrpc::HttpStatusCode::OK);
    response.setHeader("Content-Length", "999");
    response.setBody("abc");

    tinyrpc::TcpBuffer buffer;
    tinyrpc::HttpCodec codec;
    codec.encode(&buffer, &response);

    EXPECT_TRUE(response.m_encodeSucc);
    std::string raw = buffer.retrieveAllAsString();
    EXPECT_NE(raw.find("content-length: 3\r\n"), std::string::npos);
    EXPECT_EQ(raw.find("content-length: 999\r\n"), std::string::npos);
}

TEST(HttpCodecTest, EncodeResponseForcesConnectionClose)
{
    tinyrpc::HttpResponse response;
    response.setHeader("Connection", "keep-alive");
    response.setBody("abc");

    tinyrpc::TcpBuffer buffer;
    tinyrpc::HttpCodec codec;
    codec.encode(&buffer, &response);

    EXPECT_TRUE(response.m_encodeSucc);
    EXPECT_EQ(response.getHeader("Connection"), "close");
    std::string raw = buffer.retrieveAllAsString();
    EXPECT_NE(raw.find("connection: close\r\n"), std::string::npos);
    EXPECT_EQ(raw.find("connection: keep-alive\r\n"), std::string::npos);
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
