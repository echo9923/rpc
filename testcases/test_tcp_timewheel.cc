/*
 * test_tcp_timewheel.cc -- 任务五十一：连接空闲超时 / 简化时间轮测试。
 */

#include "net/reactor.h"
#include "net/tcpconnection.h"
#include "net/tcpconnectiontimewheel.h"

#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <iostream>
#include <memory>
#include <sys/socket.h>
#include <thread>
#include <unistd.h>

namespace {

void waitFor(
    tinyrpc::Reactor *reactor,
    const std::function<bool()>& done,
    int maxRounds)
{
    for (int i = 0; i < maxRounds && !done(); ++i) {
        reactor->waitOnce(20);
    }
}

struct SocketPairConnection {
    tinyrpc::Reactor m_reactor;
    int m_peerFd {-1};
    std::shared_ptr<tinyrpc::TcpConnection> m_connection;

    explicit SocketPairConnection()
    {
        int socketFds[2] {-1, -1};
        // socketpair(2) 参数依次为：协议族、socket 类型、协议号、输出 fd 数组。
        // AF_UNIX + SOCK_STREAM 创建一对本机全双工流式 socket，适合在单元测试里
        // 模拟一个可关闭的连接 fd，而不需要启动真实 TCP server。
        if (socketpair(AF_UNIX, SOCK_STREAM, 0, socketFds) == 0) {
            m_connection = std::make_shared<tinyrpc::TcpConnection>(socketFds[0], &m_reactor);
            m_peerFd = socketFds[1];
        }
    }

    ~SocketPairConnection()
    {
        if (m_connection != nullptr) {
            m_connection->closeConnection();
        }
        if (m_peerFd >= 0) {
            close(m_peerFd);
            m_peerFd = -1;
        }
    }
};

}

TEST(TcpConnectionTimeWheelTest, ActiveConnectionIsKeptAliveByRefresh)
{
    SocketPairConnection fixture;
    ASSERT_NE(fixture.m_connection, nullptr);
    ASSERT_GE(fixture.m_peerFd, 0);

    tinyrpc::TcpConnectionTimeWheel timeWheel(&fixture.m_reactor, 80, 10);
    int fd = fixture.m_connection->getFd();
    ASSERT_TRUE(timeWheel.addConnection(fixture.m_connection));

    waitFor(&fixture.m_reactor, []() { return false; }, 2);
    ASSERT_TRUE(timeWheel.refreshConnection(fd));
    waitFor(&fixture.m_reactor, []() { return false; }, 3);

    EXPECT_FALSE(fixture.m_connection->isClosed());
    EXPECT_TRUE(timeWheel.hasConnection(fd));
}

TEST(TcpConnectionTimeWheelTest, IdleConnectionIsClosedAndRemoved)
{
    SocketPairConnection fixture;
    ASSERT_NE(fixture.m_connection, nullptr);
    ASSERT_GE(fixture.m_peerFd, 0);

    std::atomic<int> closeCount {0};
    int closedFd = fixture.m_connection->getFd();
    fixture.m_connection->setCloseCallback([&closeCount, closedFd](int fd) {
        EXPECT_EQ(fd, closedFd);
        closeCount.fetch_add(1);
    });

    tinyrpc::TcpConnectionTimeWheel timeWheel(&fixture.m_reactor, 30, 10);
    ASSERT_TRUE(timeWheel.addConnection(fixture.m_connection));

    waitFor(&fixture.m_reactor, [&fixture]() {
        return fixture.m_connection->isClosed();
    }, 20);

    EXPECT_TRUE(fixture.m_connection->isClosed());
    EXPECT_EQ(fixture.m_connection->getFd(), tinyrpc::kInvalidSocket);
    EXPECT_EQ(closeCount.load(), 1);
    EXPECT_FALSE(timeWheel.hasConnection(closedFd));
}

TEST(TcpConnectionTimeWheelTest, TimeoutCloseRunsOnConnectionReactorThread)
{
    SocketPairConnection fixture;
    ASSERT_NE(fixture.m_connection, nullptr);
    ASSERT_GE(fixture.m_peerFd, 0);

    std::atomic<bool> closeCalled {false};
    std::thread::id loopThreadId;
    std::thread::id closeThreadId;
    fixture.m_connection->setCloseCallback([&](int /*fd*/) {
        closeThreadId = std::this_thread::get_id();
        closeCalled.store(true);
        fixture.m_reactor.stop();
    });

    tinyrpc::TcpConnectionTimeWheel timeWheel(&fixture.m_reactor, 30, 10);
    ASSERT_TRUE(timeWheel.addConnection(fixture.m_connection));

    std::thread loopThread([&]() {
        loopThreadId = std::this_thread::get_id();
        fixture.m_reactor.loop();
    });

    for (int i = 0; i < 30 && !closeCalled.load(); ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    if (!closeCalled.load()) {
        fixture.m_reactor.stop();
    }
    loopThread.join();

    EXPECT_TRUE(closeCalled.load());
    EXPECT_TRUE(fixture.m_connection->isClosed());
    EXPECT_EQ(closeThreadId, loopThreadId);
}

int main(int argc, char **argv)
{
    ::testing::InitGoogleTest(&argc, argv);
    int result = RUN_ALL_TESTS();
    if (result == 0) {
        std::cout << "[tcp_timewheel] PASS" << std::endl;
    }
    return result;
}
