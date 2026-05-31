/*
 * test_tcp_client.cc — 任务三十六/三十七：最小同步 TcpClient 验收测试。
 * 测试覆盖：
 *   1. 构造函数保存对端地址，初始状态为未连接
 *   2. 连接不存在的端口返回失败，getErrorInfo() 非空
 *   3. 连接正在监听的端口成功，isConnected() == true
 *   4. closeConnection() 后 isConnected() == false
 *   5. 析构函数自动关闭连接
 *   6. closeConnection() 后可重新连接同一地址
 *   7. TcpClient 能发送可被 TinyPbCodec 解码的请求帧
 *   8. TcpClient 能读取并解码 TinyPB 响应帧
 *   9. TcpClient 能完成一次最小 TinyPB 同步请求/响应闭环
 *  10. TcpClient 拒绝缺少必要字段的 TinyPB 请求且不写出非法帧
 */

#include "comm/errorcode.h"
#include "net/tcpclient.h"
#include "net/tcpbuffer.h"
#include "net/tinypb/tinypbcodec.h"

#include <gtest/gtest.h>

#include <arpa/inet.h>
#include <cerrno>
#include <cstring>
#include <poll.h>
#include <sys/socket.h>
#include <thread>
#include <unistd.h>

#include <chrono>
#include <string>

namespace {

// 关闭测试 fd，忽略关闭失败；测试主断言关注业务收发结果。
void closeIfValid(int *fd)
{
    if (*fd >= 0) {
        close(*fd);
        *fd = -1;
    }
}

// 循环读取 socket 字节，追加到 TcpBuffer，并反复尝试 TinyPbCodec::decode()。
// read(2) 参数依次为：socket fd、接收缓冲区地址、最大读取字节数。
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

// 循环写出全部字节，避免短写导致测试服务端只发出半个 TinyPB 响应。
// write(2) 参数依次为：socket fd、待发送缓冲区地址、待发送字节数。
bool writeAllToFd(int fd, const char *data, size_t len, std::string *errorInfo)
{
    size_t written = 0;
    while (written < len) {
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

} // namespace

// 在测试内部创建一个临时监听 socket，用于验证 connect 成功路径。
// bind 到 127.0.0.1:0 让内核分配空闲端口，通过 getsockname() 取回实际端口号。
class TcpClientTest : public ::testing::Test {
 protected:
    void SetUp() override
    {
        // AF_INET: IPv4; SOCK_STREAM: TCP; 0: 自动选择协议
        m_listenFd = socket(AF_INET, SOCK_STREAM, 0);
        ASSERT_GE(m_listenFd, 0) << "create listen socket failed: " << std::strerror(errno);

        sockaddr_in addr {};
        addr.sin_family = AF_INET;
        addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        addr.sin_port = htons(0);

        int rt = bind(m_listenFd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr));
        ASSERT_EQ(rt, 0) << "bind failed: " << std::strerror(errno);

        rt = listen(m_listenFd, 1);
        ASSERT_EQ(rt, 0) << "listen failed: " << std::strerror(errno);

        // 通过 getsockname() 获取内核分配的实际端口号
        socklen_t len = sizeof(m_listenAddr);
        rt = getsockname(m_listenFd, reinterpret_cast<sockaddr*>(&m_listenAddr), &len);
        ASSERT_EQ(rt, 0) << "getsockname failed: " << std::strerror(errno);
    }

    void TearDown() override
    {
        if (m_listenFd >= 0) {
            close(m_listenFd);
        }
    }

    // 获取监听地址的端口号
    uint16_t getListenPort() const
    {
        return ntohs(m_listenAddr.sin_port);
    }

    int m_listenFd {-1};
    sockaddr_in m_listenAddr {};
};

// 1. 构造函数保存对端地址，初始状态为未连接
TEST_F(TcpClientTest, ConstructorSavesPeerAddress)
{
    tinyrpc::IPAddress peerAddr("127.0.0.1", 19999);
    tinyrpc::TcpClient client(peerAddr);

    EXPECT_EQ(client.getPeerAddress().getIp(), "127.0.0.1");
    EXPECT_EQ(client.getPeerAddress().getPort(), 19999);
}

TEST_F(TcpClientTest, InitiallyNotConnected)
{
    tinyrpc::IPAddress peerAddr("127.0.0.1", 19999);
    tinyrpc::TcpClient client(peerAddr);

    EXPECT_FALSE(client.isConnected());
    EXPECT_EQ(client.getFd(), tinyrpc::kInvalidSocket);
    EXPECT_TRUE(client.getErrorInfo().empty());
}

// 2. 连接不存在的端口返回失败，getErrorInfo() 返回错误描述
TEST_F(TcpClientTest, ConnectToNonListeningPort)
{
    // socket(2) 创建一个 TCP fd；参数含义同 fixture 中的监听 socket。
    int nonListenFd = socket(AF_INET, SOCK_STREAM, 0);
    ASSERT_GE(nonListenFd, 0) << "create non-listen socket failed: " << std::strerror(errno);

    sockaddr_in nonListenAddr {};
    nonListenAddr.sin_family = AF_INET;
    nonListenAddr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    nonListenAddr.sin_port = htons(0);

    // bind(2) 只占用本机端口但不调用 listen(2)。保持 fd 打开可避免客户端
    // 自动选择同一端口作为源端口产生 TCP self-connect。
    int rt = bind(nonListenFd, reinterpret_cast<sockaddr*>(&nonListenAddr), sizeof(nonListenAddr));
    ASSERT_EQ(rt, 0) << "bind non-listen socket failed: " << std::strerror(errno);

    socklen_t len = sizeof(nonListenAddr);
    rt = getsockname(nonListenFd, reinterpret_cast<sockaddr*>(&nonListenAddr), &len);
    ASSERT_EQ(rt, 0) << "getsockname non-listen socket failed: " << std::strerror(errno);

    tinyrpc::IPAddress peerAddr("127.0.0.1", ntohs(nonListenAddr.sin_port));
    tinyrpc::TcpClient client(peerAddr);

    bool result = client.connectServer();
    EXPECT_FALSE(result);
    EXPECT_FALSE(client.isConnected());
    EXPECT_EQ(client.getFd(), tinyrpc::kInvalidSocket);
    EXPECT_FALSE(client.getErrorInfo().empty());

    // close(2) 释放仅用于占用端口的 fd；参数是要关闭的文件描述符。
    EXPECT_EQ(close(nonListenFd), 0) << "close non-listen socket failed: " << std::strerror(errno);
}

// 3. closeConnection() 后再次连接同一地址仍能成功
TEST_F(TcpClientTest, ReconnectAfterClose)
{
    tinyrpc::IPAddress peerAddr("127.0.0.1", getListenPort());
    tinyrpc::TcpClient client(peerAddr);

    ASSERT_TRUE(client.connectServer());
    ASSERT_TRUE(client.isConnected());

    client.closeConnection();
    EXPECT_FALSE(client.isConnected());

    // 再次连接同一地址，应成功
    bool result = client.connectServer();
    EXPECT_TRUE(result);
    EXPECT_TRUE(client.isConnected());
    EXPECT_GE(client.getFd(), 0);
}

// 4. 连接正在监听的端口成功
TEST_F(TcpClientTest, ConnectToListeningPort)
{
    tinyrpc::IPAddress peerAddr("127.0.0.1", getListenPort());
    tinyrpc::TcpClient client(peerAddr);

    bool result = client.connectServer();
    EXPECT_TRUE(result);
    EXPECT_TRUE(client.isConnected());
    EXPECT_GE(client.getFd(), 0);
    EXPECT_TRUE(client.getErrorInfo().empty());

    // 重复调用 connectServer() 应直接返回 true
    EXPECT_TRUE(client.connectServer());
}

// 5. closeConnection() 后 isConnected() == false
TEST_F(TcpClientTest, CloseThenNotConnected)
{
    tinyrpc::IPAddress peerAddr("127.0.0.1", getListenPort());
    tinyrpc::TcpClient client(peerAddr);

    ASSERT_TRUE(client.connectServer());
    ASSERT_TRUE(client.isConnected());

    client.closeConnection();
    EXPECT_FALSE(client.isConnected());
    EXPECT_EQ(client.getFd(), tinyrpc::kInvalidSocket);

    // 多次 closeConnection() 安全
    client.closeConnection();
    EXPECT_FALSE(client.isConnected());
}

// 6. 析构函数自动关闭连接（不出错即通过）
TEST_F(TcpClientTest, DestructorClosesConnection)
{
    tinyrpc::IPAddress peerAddr("127.0.0.1", getListenPort());
    {
        tinyrpc::TcpClient client(peerAddr);
        ASSERT_TRUE(client.connectServer());
        ASSERT_TRUE(client.isConnected());
        // client 离开作用域，析构函数应自动关闭 socket
    }
    // 若析构未正确关闭，也不会 crash，此测试验证编译和基本生命周期
}

TEST_F(TcpClientTest, SendTinyPbRequestWritesFrame)
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

        serverOk = readTinyPbFromFd(clientFd, &decodedRequest, &serverError);
        closeIfValid(&clientFd);
    });

    tinyrpc::IPAddress peerAddr("127.0.0.1", getListenPort());
    tinyrpc::TcpClient client(peerAddr);

    tinyrpc::TinyPbStruct request;
    request.m_msgReq = "client-req-1";
    request.m_serviceFullName = "QueryService.query_name";
    request.m_pbData = "request-payload";

    bool clientOk = client.sendTinyPbRequest(&request);
    std::string clientError = client.getErrorInfo();
    client.closeConnection();
    serverThread.join();

    ASSERT_TRUE(clientOk) << clientError;
    ASSERT_TRUE(serverOk) << serverError;
    EXPECT_TRUE(decodedRequest.m_decodeSucc);
    EXPECT_EQ(decodedRequest.m_msgReq, "client-req-1");
    EXPECT_EQ(decodedRequest.m_serviceFullName, "QueryService.query_name");
    EXPECT_EQ(decodedRequest.m_pbData, "request-payload");
}

TEST_F(TcpClientTest, RecvTinyPbResponseDecodesFrame)
{
    bool serverOk = false;
    std::string serverError;

    std::thread serverThread([&]() {
        int clientFd = accept(m_listenFd, nullptr, nullptr);
        if (clientFd < 0) {
            serverError = std::strerror(errno);
            return;
        }

        tinyrpc::TinyPbStruct response;
        response.m_msgReq = "client-req-2";
        response.m_serviceFullName = "QueryService.query_name";
        response.m_errCode = 0;
        response.m_pbData = "response-payload";

        std::string frame;
        if (!encodeTinyPbToString(&response, &frame)) {
            serverError = "encode response failed";
            closeIfValid(&clientFd);
            return;
        }

        serverOk = writeAllToFd(clientFd, frame.data(), frame.size(), &serverError);
        closeIfValid(&clientFd);
    });

    tinyrpc::IPAddress peerAddr("127.0.0.1", getListenPort());
    tinyrpc::TcpClient client(peerAddr);
    ASSERT_TRUE(client.connectServer()) << client.getErrorInfo();

    tinyrpc::TinyPbStruct response;
    bool clientOk = client.recvTinyPbResponse(&response);
    std::string clientError = client.getErrorInfo();
    serverThread.join();

    ASSERT_TRUE(clientOk) << clientError;
    ASSERT_TRUE(serverOk) << serverError;
    EXPECT_TRUE(response.m_decodeSucc);
    EXPECT_EQ(response.m_msgReq, "client-req-2");
    EXPECT_EQ(response.m_serviceFullName, "QueryService.query_name");
    EXPECT_EQ(response.m_errCode, 0);
    EXPECT_EQ(response.m_pbData, "response-payload");
}

TEST_F(TcpClientTest, SendAndRecvTinyPbRoundTrip)
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

        tinyrpc::TinyPbStruct response;
        response.m_msgReq = decodedRequest.m_msgReq;
        response.m_serviceFullName = decodedRequest.m_serviceFullName;
        response.m_errCode = 0;
        response.m_pbData = "roundtrip-response";

        std::string frame;
        if (!encodeTinyPbToString(&response, &frame)) {
            serverError = "encode roundtrip response failed";
            closeIfValid(&clientFd);
            return;
        }

        serverOk = writeAllToFd(clientFd, frame.data(), frame.size(), &serverError);
        closeIfValid(&clientFd);
    });

    tinyrpc::IPAddress peerAddr("127.0.0.1", getListenPort());
    tinyrpc::TcpClient client(peerAddr);

    tinyrpc::TinyPbStruct request;
    request.m_msgReq = "client-req-3";
    request.m_serviceFullName = "QueryService.query_name";
    request.m_pbData = "roundtrip-request";

    tinyrpc::TinyPbStruct response;
    bool clientOk = client.sendAndRecvTinyPb(&request, &response);
    std::string clientError = client.getErrorInfo();
    serverThread.join();

    ASSERT_TRUE(clientOk) << clientError;
    ASSERT_TRUE(serverOk) << serverError;
    EXPECT_EQ(decodedRequest.m_msgReq, "client-req-3");
    EXPECT_EQ(decodedRequest.m_serviceFullName, "QueryService.query_name");
    EXPECT_EQ(decodedRequest.m_pbData, "roundtrip-request");
    EXPECT_TRUE(response.m_decodeSucc);
    EXPECT_EQ(response.m_msgReq, "client-req-3");
    EXPECT_EQ(response.m_serviceFullName, "QueryService.query_name");
    EXPECT_EQ(response.m_errCode, 0);
    EXPECT_EQ(response.m_pbData, "roundtrip-response");
}

TEST_F(TcpClientTest, SendTinyPbRequestRejectsInvalidRequest)
{
    auto runInvalidRequest = [this](tinyrpc::TinyPbStruct *request) {
        bool serverSawData = true;
        bool serverOk = false;
        std::string serverError;

        std::thread serverThread([&]() {
            int clientFd = accept(m_listenFd, nullptr, nullptr);
            if (clientFd < 0) {
                serverError = std::strerror(errno);
                return;
            }

            pollfd pfd {};
            pfd.fd = clientFd;
            pfd.events = POLLIN;

            // poll(2) 等待 fd 事件；参数依次为 pollfd 数组、数组长度、超时时间毫秒。
            int rt = poll(&pfd, 1, 100);
            if (rt < 0) {
                serverError = std::strerror(errno);
                closeIfValid(&clientFd);
                return;
            }

            if (rt == 0) {
                serverSawData = false;
            } else {
                char data[1];
                // recv(2) 参数依次为：socket fd、接收缓冲区地址、最大读取字节数、读取标志。
                // MSG_DONTWAIT 表示本次读取不阻塞；客户端关闭导致的 EOF 不算写出了非法帧。
                ssize_t n = recv(clientFd, data, sizeof(data), MSG_DONTWAIT);
                if (n > 0) {
                    serverSawData = true;
                } else if (n == 0 || errno == EAGAIN || errno == EWOULDBLOCK) {
                    serverSawData = false;
                } else {
                    serverError = std::strerror(errno);
                    closeIfValid(&clientFd);
                    return;
                }
            }
            serverOk = true;
            closeIfValid(&clientFd);
        });

        tinyrpc::IPAddress peerAddr("127.0.0.1", getListenPort());
        tinyrpc::TcpClient client(peerAddr);

        bool clientOk = client.sendTinyPbRequest(request);
        std::string clientError = client.getErrorInfo();
        client.closeConnection();
        serverThread.join();

        EXPECT_FALSE(clientOk);
        EXPECT_FALSE(request->m_encodeSucc);
        EXPECT_FALSE(clientError.empty());
        EXPECT_TRUE(serverOk) << serverError;
        EXPECT_FALSE(serverSawData);
    };

    tinyrpc::TinyPbStruct emptyMsgReq;
    emptyMsgReq.m_msgReq = "";
    emptyMsgReq.m_serviceFullName = "QueryService.query_name";
    emptyMsgReq.m_pbData = "invalid-request";
    runInvalidRequest(&emptyMsgReq);

    tinyrpc::TinyPbStruct emptyServiceName;
    emptyServiceName.m_msgReq = "client-req-invalid";
    emptyServiceName.m_serviceFullName = "";
    emptyServiceName.m_pbData = "invalid-request";
    runInvalidRequest(&emptyServiceName);
}

TEST_F(TcpClientTest, RecvTinyPbResponseTimesOutWhenServerDoesNotReply)
{
    bool serverAccepted = false;
    std::string serverError;

    std::thread serverThread([&]() {
        int clientFd = accept(m_listenFd, nullptr, nullptr);
        if (clientFd < 0) {
            serverError = std::strerror(errno);
            return;
        }

        serverAccepted = true;
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
        closeIfValid(&clientFd);
    });

    tinyrpc::IPAddress peerAddr("127.0.0.1", getListenPort());
    tinyrpc::TcpClient client(peerAddr);
    client.setTimeout(50);
    ASSERT_TRUE(client.connectServer()) << client.getErrorInfo();

    tinyrpc::TinyPbStruct response;
    bool clientOk = client.recvTinyPbResponse(&response);
    std::string clientError = client.getErrorInfo();
    serverThread.join();

    ASSERT_TRUE(serverAccepted) << serverError;
    EXPECT_FALSE(clientOk);
    EXPECT_EQ(client.getErrorCode(), tinyrpc::ERROR_TCP_TIMEOUT);
    EXPECT_FALSE(clientError.empty());
}

TEST_F(TcpClientTest, RecvTinyPbResponseFailsWhenServerClosesEarly)
{
    bool serverClosed = false;
    std::string serverError;

    std::thread serverThread([&]() {
        int clientFd = accept(m_listenFd, nullptr, nullptr);
        if (clientFd < 0) {
            serverError = std::strerror(errno);
            return;
        }

        serverClosed = true;
        closeIfValid(&clientFd);
    });

    tinyrpc::IPAddress peerAddr("127.0.0.1", getListenPort());
    tinyrpc::TcpClient client(peerAddr);
    client.setTimeout(500);
    ASSERT_TRUE(client.connectServer()) << client.getErrorInfo();

    tinyrpc::TinyPbStruct response;
    bool clientOk = client.recvTinyPbResponse(&response);
    std::string clientError = client.getErrorInfo();
    serverThread.join();

    ASSERT_TRUE(serverClosed) << serverError;
    EXPECT_FALSE(clientOk);
    EXPECT_EQ(client.getErrorCode(), tinyrpc::ERROR_TCP_RECV_FAILED);
    EXPECT_FALSE(clientError.empty());
}

TEST_F(TcpClientTest, SlowResponseBeforeTimeoutSucceeds)
{
    bool serverOk = false;
    std::string serverError;

    std::thread serverThread([&]() {
        int clientFd = accept(m_listenFd, nullptr, nullptr);
        if (clientFd < 0) {
            serverError = std::strerror(errno);
            return;
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(50));

        tinyrpc::TinyPbStruct response;
        response.m_msgReq = "slow-ok";
        response.m_serviceFullName = "QueryService.query_name";
        response.m_errCode = 0;
        response.m_pbData = "slow-response";

        std::string frame;
        if (!encodeTinyPbToString(&response, &frame)) {
            serverError = "encode slow response failed";
            closeIfValid(&clientFd);
            return;
        }

        serverOk = writeAllToFd(clientFd, frame.data(), frame.size(), &serverError);
        closeIfValid(&clientFd);
    });

    tinyrpc::IPAddress peerAddr("127.0.0.1", getListenPort());
    tinyrpc::TcpClient client(peerAddr);
    client.setTimeout(500);
    ASSERT_TRUE(client.connectServer()) << client.getErrorInfo();

    tinyrpc::TinyPbStruct response;
    bool clientOk = client.recvTinyPbResponse(&response);
    std::string clientError = client.getErrorInfo();
    serverThread.join();

    ASSERT_TRUE(serverOk) << serverError;
    ASSERT_TRUE(clientOk) << clientError;
    EXPECT_EQ(client.getErrorCode(), 0);
    EXPECT_EQ(response.m_msgReq, "slow-ok");
    EXPECT_EQ(response.m_pbData, "slow-response");
}

int main(int argc, char **argv)
{
    ::testing::InitGoogleTest(&argc, argv);
    int result = RUN_ALL_TESTS();
    if (result == 0) {
        std::cout << "[tcp_client] PASS" << std::endl;
    }
    return result;
}
