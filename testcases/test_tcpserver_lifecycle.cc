/*
 * test_tcpserver_lifecycle.cc -- 阶段 26：TcpServer 优雅停止和生命周期测试。
 */

#include "net/netaddress.h"
#include "net/tcpclient.h"
#include "net/tcpserver.h"

#include <gtest/gtest.h>

#include <arpa/inet.h>
#include <cerrno>
#include <chrono>
#include <cstring>
#include <iostream>
#include <string>
#include <sys/socket.h>
#include <thread>
#include <unistd.h>

namespace {

void closeIfValid(int *fd)
{
    if (*fd >= 0) {
        close(*fd);
        *fd = -1;
    }
}

uint16_t reserveFreePort()
{
    // socket(2) 创建临时 TCP fd；AF_INET 表示 IPv4，SOCK_STREAM 表示 TCP 字节流。
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) {
        return 0;
    }

    sockaddr_in addr {};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    addr.sin_port = htons(0);

    // bind(2) 绑定 127.0.0.1:0，让内核分配一个当前可用端口。
    if (bind(fd, reinterpret_cast<sockaddr *>(&addr), sizeof(addr)) != 0) {
        closeIfValid(&fd);
        return 0;
    }

    socklen_t len = sizeof(addr);
    // getsockname(2) 读取内核实际分配的端口，输出到 addr。
    if (getsockname(fd, reinterpret_cast<sockaddr *>(&addr), &len) != 0) {
        closeIfValid(&fd);
        return 0;
    }

    uint16_t port = ntohs(addr.sin_port);
    closeIfValid(&fd);
    return port;
}

bool waitUntilReady(uint16_t port)
{
    for (int i = 0; i < 80; ++i) {
        tinyrpc::TcpClient client(tinyrpc::IPAddress("127.0.0.1", port));
        if (client.connectServer()) {
            client.closeConnection();
            return true;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    return false;
}

void runServerThread(tinyrpc::TcpServer *server)
{
    server->start();
}

}  // namespace

TEST(TcpServerLifecycleTest, StopWakesBlockingStartLoop)
{
    uint16_t port = reserveFreePort();
    ASSERT_NE(port, 0);

    tinyrpc::TcpServer server(tinyrpc::IPAddress("127.0.0.1", port));
    ASSERT_TRUE(server.init());

    std::thread serverThread(runServerThread, &server);
    ASSERT_TRUE(waitUntilReady(port));
    EXPECT_TRUE(server.isRunning());

    server.stop();
    serverThread.join();

    EXPECT_FALSE(server.isRunning());
    EXPECT_EQ(server.getConnectionCount(), 0u);
}

TEST(TcpServerLifecycleTest, StopClosesActiveSingleReactorConnections)
{
    uint16_t port = reserveFreePort();
    ASSERT_NE(port, 0);

    tinyrpc::TcpServer server(tinyrpc::IPAddress("127.0.0.1", port));
    ASSERT_TRUE(server.init());

    std::thread serverThread(runServerThread, &server);
    ASSERT_TRUE(waitUntilReady(port));

    tinyrpc::TcpClient client(tinyrpc::IPAddress("127.0.0.1", port));
    ASSERT_TRUE(client.connectServer()) << client.getErrorInfo();

    for (int i = 0; i < 80 && server.getConnectionCount() == 0; ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    EXPECT_GE(server.getConnectionCount(), 1u);

    server.stop();
    serverThread.join();

    EXPECT_FALSE(server.isRunning());
    EXPECT_EQ(server.getConnectionCount(), 0u);
}

TEST(TcpServerLifecycleTest, StopClosesMultiReactorConnections)
{
    uint16_t port = reserveFreePort();
    ASSERT_NE(port, 0);

    tinyrpc::TcpServer server(tinyrpc::IPAddress("127.0.0.1", port));
    server.setIOThreadNum(2);
    ASSERT_TRUE(server.init());

    std::thread serverThread(runServerThread, &server);
    ASSERT_TRUE(waitUntilReady(port));

    tinyrpc::TcpClient first(tinyrpc::IPAddress("127.0.0.1", port));
    tinyrpc::TcpClient second(tinyrpc::IPAddress("127.0.0.1", port));
    ASSERT_TRUE(first.connectServer()) << first.getErrorInfo();
    ASSERT_TRUE(second.connectServer()) << second.getErrorInfo();

    for (int i = 0; i < 80 && server.getConnectionCount() < 2; ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    EXPECT_GE(server.getConnectionCount(), 2u);

    server.stop();
    serverThread.join();

    EXPECT_FALSE(server.isRunning());
    EXPECT_EQ(server.getConnectionCount(), 0u);
}

TEST(TcpServerLifecycleTest, ClosingOneMultiReactorConnectionKeepsOtherConnection)
{
    uint16_t port = reserveFreePort();
    ASSERT_NE(port, 0);

    tinyrpc::TcpServer server(tinyrpc::IPAddress("127.0.0.1", port));
    server.setIOThreadNum(2);
    ASSERT_TRUE(server.init());

    std::thread serverThread(runServerThread, &server);
    ASSERT_TRUE(waitUntilReady(port));

    tinyrpc::TcpClient first(tinyrpc::IPAddress("127.0.0.1", port));
    tinyrpc::TcpClient second(tinyrpc::IPAddress("127.0.0.1", port));
    ASSERT_TRUE(first.connectServer()) << first.getErrorInfo();
    ASSERT_TRUE(second.connectServer()) << second.getErrorInfo();

    for (int i = 0; i < 80 && server.getConnectionCount() < 2; ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    ASSERT_GE(server.getConnectionCount(), 2u);

    first.closeConnection();
    for (int i = 0; i < 80 && server.getConnectionCount() != 1; ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    EXPECT_EQ(server.getConnectionCount(), 1u);
    EXPECT_TRUE(second.isConnected());

    second.closeConnection();
    server.stop();
    serverThread.join();
}

TEST(TcpServerLifecycleTest, StopIsIdempotentAndReleasesPort)
{
    uint16_t port = reserveFreePort();
    ASSERT_NE(port, 0);

    {
        tinyrpc::TcpServer server(tinyrpc::IPAddress("127.0.0.1", port));
        ASSERT_TRUE(server.init());

        std::thread serverThread(runServerThread, &server);
        ASSERT_TRUE(waitUntilReady(port));

        server.stop();
        server.stop();
        serverThread.join();
        server.stop();
        EXPECT_FALSE(server.isRunning());
    }

    tinyrpc::TcpServer restarted(tinyrpc::IPAddress("127.0.0.1", port));
    EXPECT_TRUE(restarted.init());
    restarted.stop();
}

TEST(TcpServerLifecycleTest, ClosedPeerDoesNotKeepServerConnectionAlive)
{
    uint16_t port = reserveFreePort();
    ASSERT_NE(port, 0);

    int aliveBefore = tinyrpc::TcpConnection::getAliveCountForTest();
    tinyrpc::TcpServer server(tinyrpc::IPAddress("127.0.0.1", port));
    ASSERT_TRUE(server.init());

    std::thread serverThread(runServerThread, &server);
    ASSERT_TRUE(waitUntilReady(port));

    tinyrpc::TcpClient client(tinyrpc::IPAddress("127.0.0.1", port));
    ASSERT_TRUE(client.connectServer()) << client.getErrorInfo();
    client.closeConnection();

    for (int i = 0;
         i < 100
         && (server.getConnectionCount() != 0
             || tinyrpc::TcpConnection::getAliveCountForTest() != aliveBefore);
         ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    EXPECT_EQ(server.getConnectionCount(), 0u);
    EXPECT_EQ(tinyrpc::TcpConnection::getAliveCountForTest(), aliveBefore);

    server.stop();
    serverThread.join();
}

int main(int argc, char **argv)
{
    ::testing::InitGoogleTest(&argc, argv);
    int result = RUN_ALL_TESTS();
    if (result == 0) {
        std::cout << "[tcpserver-lifecycle] PASS" << std::endl;
    }
    return result;
}
