/*
 * test_tcp_client.cc — 任务三十六：最小同步 TcpClient 连接骨架验收测试。
 * 测试覆盖：
 *   1. 构造函数保存对端地址，初始状态为未连接
 *   2. 连接不存在的端口返回失败，getErrorInfo() 非空
 *   3. 连接正在监听的端口成功，isConnected() == true
 *   4. closeConnection() 后 isConnected() == false
 *   5. 析构函数自动关闭连接
 *   6. closeConnection() 后可重新连接同一地址
 */

#include "net/tcpclient.h"

#include <gtest/gtest.h>

#include <arpa/inet.h>
#include <cerrno>
#include <cstring>
#include <sys/socket.h>
#include <unistd.h>

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

int main(int argc, char **argv)
{
    ::testing::InitGoogleTest(&argc, argv);
    int result = RUN_ALL_TESTS();
    if (result == 0) {
        std::cout << "[tcp_client] PASS" << std::endl;
    }
    return result;
}
