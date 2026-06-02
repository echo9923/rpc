/*
 * test_hook_socket.cc -- 任务七十一：recv/send/accept hook 的协程恢复测试。
 */

#include "coroutine/coroutine.h"
#include "coroutine/coroutine_hook.h"
#include "net/fdevent.h"
#include "net/reactor.h"
#include "net/timer.h"

#include <gtest/gtest.h>

#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <functional>
#include <iostream>
#include <netinet/in.h>
#include <sys/epoll.h>
#include <sys/socket.h>
#include <unistd.h>
#include <vector>

namespace {

void setNonBlockLocal(int fd)
{
    int flags = fcntl(fd, F_GETFL, 0);
    ASSERT_GE(flags, 0);
    ASSERT_EQ(fcntl(fd, F_SETFL, flags | O_NONBLOCK), 0);
}

void driveReactorUntil(tinyrpc::Reactor *reactor, const std::function<bool()>& done, int timeoutMs)
{
    int64_t deadline = tinyrpc::getNowMs() + timeoutMs;
    while (!done() && tinyrpc::getNowMs() < deadline) {
        reactor->waitOnce(100);
    }
}

int createListenSocket(uint16_t *port)
{
    int listenFd = socket(AF_INET, SOCK_STREAM, 0);
    EXPECT_GE(listenFd, 0);

    int reuse = 1;
    EXPECT_EQ(setsockopt(listenFd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse)), 0);

    sockaddr_in addr {};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    addr.sin_port = 0;

    EXPECT_EQ(bind(listenFd, reinterpret_cast<sockaddr *>(&addr), sizeof(addr)), 0);
    EXPECT_EQ(listen(listenFd, 8), 0);

    socklen_t len = sizeof(addr);
    EXPECT_EQ(getsockname(listenFd, reinterpret_cast<sockaddr *>(&addr), &len), 0);
    *port = ntohs(addr.sin_port);
    return listenFd;
}

sockaddr_in makeLoopbackAddr(uint16_t port)
{
    sockaddr_in addr {};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    addr.sin_port = htons(port);
    return addr;
}

void drainSocket(int fd)
{
    std::vector<char> buffer(64 * 1024);
    while (true) {
        // recv(2) 参数依次为：socket fd、接收缓冲区、缓冲区长度、flags。
        // 这里以非阻塞方式清空 peer socket 中已到达的数据，给另一端 send 缓冲区腾空间。
        ssize_t n = ::recv(fd, buffer.data(), buffer.size(), 0);
        if (n > 0) {
            continue;
        }
        if (n < 0 && errno == EINTR) {
            continue;
        }
        if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
            return;
        }
        return;
    }
}

void fillSendBufferUntilEagain(int fd)
{
    std::vector<char> payload(64 * 1024, 'x');
    while (true) {
        // send(2) 参数依次为：socket fd、待发送缓冲区、字节数、flags。
        // MSG_NOSIGNAL 避免对端异常关闭时触发 SIGPIPE；本测试只关注 EAGAIN。
        ssize_t n = ::send(fd, payload.data(), payload.size(), MSG_NOSIGNAL);
        if (n > 0) {
            continue;
        }
        if (n < 0 && errno == EINTR) {
            continue;
        }
        ASSERT_LT(n, 0);
        ASSERT_TRUE(errno == EAGAIN || errno == EWOULDBLOCK) << "errno = " << errno;
        return;
    }
}

}

TEST(HookSocketTest, RecvHookYieldsAndReadsAfterPeerSend)
{
    int fds[2];
    ASSERT_EQ(socketpair(AF_UNIX, SOCK_STREAM, 0, fds), 0);
    setNonBlockLocal(fds[0]);

    tinyrpc::Reactor reactor;
    tinyrpc::FdEvent event(fds[0]);
    event.setReactor(&reactor);

    char buffer[32] {};
    ssize_t recvResult = 0;
    bool done = false;

    tinyrpc::Coroutine co([&]() {
        recvResult = tinyrpc::recv_hook(&event, buffer, sizeof(buffer), 0, 1000);
        done = true;
    });

    co.resume();

    ASSERT_EQ(co.getState(), tinyrpc::CoroutineState::Suspended);
    EXPECT_EQ(event.getCoroutine(), &co);
    EXPECT_TRUE(event.getListenEvents() & EPOLLIN);

    const char *msg = "recv-ready";
    ASSERT_EQ(::send(fds[1], msg, strlen(msg), MSG_NOSIGNAL), static_cast<ssize_t>(strlen(msg)));

    driveReactorUntil(&reactor, [&done]() { return done; }, 1000);

    ASSERT_TRUE(done);
    EXPECT_EQ(recvResult, static_cast<ssize_t>(strlen(msg)));
    EXPECT_EQ(strncmp(buffer, msg, strlen(msg)), 0);
    EXPECT_EQ(co.getState(), tinyrpc::CoroutineState::Finished);

    event.unregisterFromReactor();
    close(fds[0]);
    close(fds[1]);
}

TEST(HookSocketTest, RecvHookTimeoutResumesCoroutine)
{
    int fds[2];
    ASSERT_EQ(socketpair(AF_UNIX, SOCK_STREAM, 0, fds), 0);
    setNonBlockLocal(fds[0]);

    tinyrpc::Reactor reactor;
    tinyrpc::FdEvent event(fds[0]);
    event.setReactor(&reactor);

    char buffer[8] {};
    ssize_t recvResult = 0;
    int recvErrno = 0;
    bool done = false;

    tinyrpc::Coroutine co([&]() {
        recvResult = tinyrpc::recv_hook(&event, buffer, sizeof(buffer), 0, 20);
        recvErrno = errno;
        done = true;
    });

    co.resume();
    ASSERT_EQ(co.getState(), tinyrpc::CoroutineState::Suspended);

    driveReactorUntil(&reactor, [&done]() { return done; }, 1000);

    ASSERT_TRUE(done);
    EXPECT_EQ(recvResult, -1);
    EXPECT_EQ(recvErrno, ETIMEDOUT);

    event.unregisterFromReactor();
    close(fds[0]);
    close(fds[1]);
}

TEST(HookSocketTest, SendHookYieldsAndWritesAfterPeerDrain)
{
    int fds[2];
    ASSERT_EQ(socketpair(AF_UNIX, SOCK_STREAM, 0, fds), 0);
    setNonBlockLocal(fds[0]);
    setNonBlockLocal(fds[1]);

    fillSendBufferUntilEagain(fds[0]);

    tinyrpc::Reactor reactor;
    tinyrpc::FdEvent event(fds[0]);
    event.setReactor(&reactor);

    const char *msg = "send-after-drain";
    ssize_t sendResult = 0;
    bool done = false;

    tinyrpc::Coroutine co([&]() {
        sendResult = tinyrpc::send_hook(&event, msg, strlen(msg), MSG_NOSIGNAL, 1000);
        done = true;
    });

    co.resume();

    ASSERT_EQ(co.getState(), tinyrpc::CoroutineState::Suspended);
    EXPECT_EQ(event.getCoroutine(), &co);
    EXPECT_TRUE(event.getListenEvents() & EPOLLOUT);

    drainSocket(fds[1]);
    driveReactorUntil(&reactor, [&done]() { return done; }, 1000);

    ASSERT_TRUE(done);
    EXPECT_EQ(sendResult, static_cast<ssize_t>(strlen(msg)));
    EXPECT_EQ(co.getState(), tinyrpc::CoroutineState::Finished);

    event.unregisterFromReactor();
    close(fds[0]);
    close(fds[1]);
}

TEST(HookSocketTest, AcceptHookYieldsAndAcceptsAfterClientConnect)
{
    uint16_t port = 0;
    int listenFd = createListenSocket(&port);
    ASSERT_GE(listenFd, 0);
    setNonBlockLocal(listenFd);

    tinyrpc::Reactor reactor;
    tinyrpc::FdEvent event(listenFd);
    event.setReactor(&reactor);

    sockaddr_in peerAddr {};
    socklen_t peerLen = sizeof(peerAddr);
    int acceptedFd = -1;
    bool done = false;

    tinyrpc::Coroutine co([&]() {
        acceptedFd = tinyrpc::accept_hook(
            &event,
            reinterpret_cast<sockaddr *>(&peerAddr),
            &peerLen,
            1000
        );
        done = true;
    });

    co.resume();

    ASSERT_EQ(co.getState(), tinyrpc::CoroutineState::Suspended);
    EXPECT_EQ(event.getCoroutine(), &co);
    EXPECT_TRUE(event.getListenEvents() & EPOLLIN);

    int clientFd = socket(AF_INET, SOCK_STREAM, 0);
    ASSERT_GE(clientFd, 0);
    sockaddr_in addr = makeLoopbackAddr(port);
    ASSERT_EQ(connect(clientFd, reinterpret_cast<sockaddr *>(&addr), sizeof(addr)), 0);

    driveReactorUntil(&reactor, [&done]() { return done; }, 1000);

    ASSERT_TRUE(done);
    EXPECT_GE(acceptedFd, 0);
    EXPECT_EQ(co.getState(), tinyrpc::CoroutineState::Finished);

    if (acceptedFd >= 0) {
        close(acceptedFd);
    }
    close(clientFd);
    event.unregisterFromReactor();
    close(listenFd);
}

TEST(HookSocketTest, AcceptHookTimeoutResumesCoroutine)
{
    uint16_t port = 0;
    int listenFd = createListenSocket(&port);
    ASSERT_GE(listenFd, 0);
    setNonBlockLocal(listenFd);

    tinyrpc::Reactor reactor;
    tinyrpc::FdEvent event(listenFd);
    event.setReactor(&reactor);

    int acceptedFd = 0;
    int acceptErrno = 0;
    bool done = false;

    tinyrpc::Coroutine co([&]() {
        acceptedFd = tinyrpc::accept_hook(&event, nullptr, nullptr, 20);
        acceptErrno = errno;
        done = true;
    });

    co.resume();
    ASSERT_EQ(co.getState(), tinyrpc::CoroutineState::Suspended);

    driveReactorUntil(&reactor, [&done]() { return done; }, 1000);

    ASSERT_TRUE(done);
    EXPECT_EQ(acceptedFd, -1);
    EXPECT_EQ(acceptErrno, ETIMEDOUT);

    event.unregisterFromReactor();
    close(listenFd);
}

TEST(HookSocketTest, MainCoroutineUsesRawRecvSendAccept)
{
    ASSERT_TRUE(tinyrpc::Coroutine::IsMainCoroutine());

    int fds[2];
    ASSERT_EQ(socketpair(AF_UNIX, SOCK_STREAM, 0, fds), 0);

    tinyrpc::FdEvent recvEvent(fds[0]);
    tinyrpc::FdEvent sendEvent(fds[1]);

    const char *msg = "raw";
    EXPECT_EQ(tinyrpc::send_hook(&sendEvent, msg, strlen(msg), MSG_NOSIGNAL), static_cast<ssize_t>(strlen(msg)));

    char buffer[8] {};
    EXPECT_EQ(tinyrpc::recv_hook(&recvEvent, buffer, sizeof(buffer), 0), static_cast<ssize_t>(strlen(msg)));
    EXPECT_EQ(strncmp(buffer, msg, strlen(msg)), 0);
    EXPECT_EQ(recvEvent.getCoroutine(), nullptr);
    EXPECT_EQ(sendEvent.getCoroutine(), nullptr);

    uint16_t port = 0;
    int listenFd = createListenSocket(&port);
    ASSERT_GE(listenFd, 0);
    setNonBlockLocal(listenFd);

    tinyrpc::FdEvent acceptEvent(listenFd);
    int acceptedFd = tinyrpc::accept_hook(&acceptEvent, nullptr, nullptr);
    EXPECT_EQ(acceptedFd, -1);
    EXPECT_TRUE(errno == EAGAIN || errno == EWOULDBLOCK);
    EXPECT_EQ(acceptEvent.getCoroutine(), nullptr);

    close(listenFd);
    close(fds[0]);
    close(fds[1]);
}

int main(int argc, char **argv)
{
    ::testing::InitGoogleTest(&argc, argv);
    int result = RUN_ALL_TESTS();
    if (result == 0) {
        std::cout << "[hook_socket] PASS" << std::endl;
    }
    return result;
}
