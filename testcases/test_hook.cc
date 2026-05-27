/*
 * test_hook.cc — 任务二十一：read_hook/write_hook 最小雏形验收测试。
 *
 * 测试覆盖：
 *   1. ReadHookYieldsOnEAGAIN：pipe 无数据时协程调 read_hook 后 Yield，
 *      验证协程状态、FdEvent::getCoroutine()、getListenEvents()。
 *   2. ReadHookResumesAndReads：写入数据后手动 resume，协程读到数据并 Finished。
 *   3. WriteHookInMainCoroutine：主协程调 write_hook 直通系统写，不挂载协程。
 */

#include "coroutine/coroutine.h"
#include "coroutine/coroutine_hook.h"
#include "net/fdevent.h"

#include <gtest/gtest.h>

#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <sys/epoll.h>
#include <unistd.h>

// ─────────────────────────────────────────────────────────────────────────────
// 辅助函数：将 fd 设置为非阻塞模式。
// fcntl(F_GETFL) 获取当前标志位，再用 fcntl(F_SETFL) 追加 O_NONBLOCK 标志。
// ─────────────────────────────────────────────────────────────────────────────
static void setNonBlockLocal(int fd)
{
    int flags = fcntl(fd, F_GETFL, 0);
    ASSERT_GE(flags, 0) << "fcntl F_GETFL failed";
    ASSERT_EQ(fcntl(fd, F_SETFL, flags | O_NONBLOCK), 0) << "fcntl F_SETFL failed";
}

// ─────────────────────────────────────────────────────────────────────────────
// Test 1：read_hook 在 EAGAIN 时让出执行权
// ─────────────────────────────────────────────────────────────────────────────
TEST(HookTest, ReadHookYieldsOnEAGAIN)
{
    // pipe(pipefd)：创建匿名管道，pipefd[0] 为读端，pipefd[1] 为写端。
    int pipefd[2];
    ASSERT_EQ(pipe(pipefd), 0);
    int readFd  = pipefd[0];
    int writeFd = pipefd[1];

    // 将读端设为非阻塞，保证无数据时 ::read 返回 EAGAIN 而非阻塞。
    setNonBlockLocal(readFd);

    // 直接构造 FdEvent，管理 readFd 上的事件。
    tinyrpc::FdEvent readEvent(readFd);

    char buf[64];
    bool coroDone = false;

    // 创建协程：内部调用 read_hook，预期遇到 EAGAIN 后 Yield。
    tinyrpc::Coroutine co([&]() {
        tinyrpc::read_hook(&readEvent, buf, sizeof(buf));
        coroDone = true;
    });

    // 第一次 resume：协程执行 read_hook → EAGAIN → Yield，回到主协程。
    co.resume();

    // 验证协程处于 Suspended 状态。
    EXPECT_EQ(co.getState(), tinyrpc::CoroutineState::Suspended);

    // 验证 FdEvent 上挂载了该协程。
    EXPECT_EQ(readEvent.getCoroutine(), &co);

    // 验证 FdEvent 的监听事件包含 EPOLLIN。
    EXPECT_TRUE(readEvent.getListenEvents() & EPOLLIN);

    // 协程尚未执行完毕。
    EXPECT_FALSE(coroDone);

    close(readFd);
    close(writeFd);
}

// ─────────────────────────────────────────────────────────────────────────────
// Test 2：resume 后 read_hook 读到数据，协程正常结束
// ─────────────────────────────────────────────────────────────────────────────
TEST(HookTest, ReadHookResumesAndReads)
{
    // pipe(pipefd)：创建匿名管道，pipefd[0] 为读端，pipefd[1] 为写端。
    int pipefd[2];
    ASSERT_EQ(pipe(pipefd), 0);
    int readFd  = pipefd[0];
    int writeFd = pipefd[1];

    // 将读端设为非阻塞。
    setNonBlockLocal(readFd);

    // 直接构造 FdEvent，管理 readFd 上的事件。
    tinyrpc::FdEvent readEvent(readFd);

    char buf[64];
    memset(buf, 0, sizeof(buf));
    ssize_t readResult = 0;
    bool coroDone = false;

    tinyrpc::Coroutine co([&]() {
        readResult = tinyrpc::read_hook(&readEvent, buf, sizeof(buf));
        coroDone = true;
    });

    // 第一次 resume：遇到 EAGAIN，协程 Yield。
    co.resume();
    ASSERT_EQ(co.getState(), tinyrpc::CoroutineState::Suspended);

    // 向 pipe 写端写入数据，模拟"IO 就绪"。
    // ::write(fd, buf, count)：将 count 字节从 buf 写入 fd 对应的管道写端。
    const char *msg = "hello";
    ssize_t written = ::write(writeFd, msg, strlen(msg));
    ASSERT_EQ(written, static_cast<ssize_t>(strlen(msg)));

    // 手动 resume：协程从 Yield 返回，再次 ::read，读到 "hello"。
    co.resume();

    // 协程应执行完毕。
    EXPECT_TRUE(coroDone);
    EXPECT_EQ(co.getState(), tinyrpc::CoroutineState::Finished);

    // 验证读到的字节数和内容正确。
    EXPECT_EQ(readResult, static_cast<ssize_t>(strlen(msg)));
    EXPECT_EQ(strncmp(buf, msg, strlen(msg)), 0);

    close(readFd);
    close(writeFd);
}

// ─────────────────────────────────────────────────────────────────────────────
// Test 3：主协程中 write_hook 直通系统写，不挂载协程
// ─────────────────────────────────────────────────────────────────────────────
TEST(HookTest, WriteHookInMainCoroutine)
{
    // 确认当前在主协程中。
    ASSERT_TRUE(tinyrpc::Coroutine::IsMainCoroutine());

    // pipe(pipefd)：创建匿名管道，pipefd[0] 为读端，pipefd[1] 为写端。
    int pipefd[2];
    ASSERT_EQ(pipe(pipefd), 0);
    int readFd  = pipefd[0];
    int writeFd = pipefd[1];

    // 直接构造 FdEvent，管理 writeFd 上的事件。
    tinyrpc::FdEvent writeEvent(writeFd);

    const char *msg = "world";
    // 主协程直接调用 write_hook，预期直通 ::write，返回实际写入字节数。
    ssize_t ret = tinyrpc::write_hook(&writeEvent, msg, strlen(msg));
    EXPECT_EQ(ret, static_cast<ssize_t>(strlen(msg)));

    // FdEvent 上不应挂载任何协程（主协程不做挂起）。
    EXPECT_EQ(writeEvent.getCoroutine(), nullptr);

    close(readFd);
    close(writeFd);
}

// ─────────────────────────────────────────────────────────────────────────────
// main
// ─────────────────────────────────────────────────────────────────────────────
int main(int argc, char **argv)
{
    ::testing::InitGoogleTest(&argc, argv);
    int result = RUN_ALL_TESTS();
    if (result == 0) {
        std::cout << "[hook] PASS" << std::endl;
    }
    return result;
}
