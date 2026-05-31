#include "net/fdevent.h"
#include "net/fdutil.h"
#include "net/reactor.h"

#include <atomic>
#include <chrono>
#include <sys/epoll.h>
#include <thread>
#include <unistd.h>

#include <iostream>
#include <string>
#include <vector>

namespace {

bool testFdEventReadCallbackAndDelete()
{
    int readCount = 0;
    std::string received;

    // 1. int pipe(int fd[2]) 是 POSIX 系统调用，在内核中创建一个单向数据管道。
    //    参数：一个 int[2] 数组。
    //    返回后：fd[0] = 读端（从此读取写入的数据）
    //           fd[1] = 写端（向此写入数据，内核缓存后可从读端读出）
    //    写端写入 "x" → 内核将数据放入管道缓冲区 → 读端变为可读。
    //    用它替代 TCP socket 来测试 epoll 事件通知，代码最精简。
    int pipeFds[2];
    if (pipe(pipeFds) < 0) {
        std::cerr << "[reactor] FAIL: pipe() failed" << std::endl;
        return false;
    }

    // 2. 对读端 pipeFds[0] 设置非阻塞。
    tinyrpc::setNonBlock(pipeFds[0]);

    // 3-5. 构造 FdEvent，添加 EPOLLIN，注册读回调。
    tinyrpc::FdEvent readEvent(pipeFds[0]);
    readEvent.addListenEvent(EPOLLIN);
    readEvent.setReadCallback([&readCount, &received, fd = pipeFds[0]]() {
        char buf[64];
        ssize_t n = read(fd, buf, sizeof(buf) - 1);
        if (n > 0) {
            buf[n] = '\0';
            received.append(buf, n);
        }
        ++readCount;
    });

    // 6. 构造 Reactor。
    tinyrpc::Reactor reactor;

    // 7. 将 Reactor 交给 FdEvent，由 FdEvent 负责注册自身。
    readEvent.setReactor(&reactor);
    if (!readEvent.registerToReactor()) {
        std::cerr << "[reactor] FAIL: registerToReactor failed" << std::endl;
        return false;
    }
    if (!readEvent.isRegistered()) {
        std::cerr << "[reactor] FAIL: event is not registered" << std::endl;
        return false;
    }
    if (!readEvent.updateToReactor()) {
        std::cerr << "[reactor] FAIL: updateToReactor failed" << std::endl;
        return false;
    }

    // 8. 向 pipe 写端写入 "x"。
    if (write(pipeFds[1], "x", 1) < 0) {
        std::cerr << "[reactor] FAIL: write to pipe failed" << std::endl;
        return false;
    }

    // 9. 调用 waitOnce(1000)，超时 1 秒。
    int nfds = reactor.waitOnce(1000);
    if (nfds <= 0) {
        std::cerr << "[reactor] FAIL: waitOnce returned " << nfds
                  << ", expected > 0" << std::endl;
        return false;
    }

    // 10. 验证读回调被调用一次，读取内容是 "x"。
    if (readCount != 1) {
        std::cerr << "[reactor] FAIL: readCount != 1, got " << readCount
                  << std::endl;
        return false;
    }
    if (received != "x") {
        std::cerr << "[reactor] FAIL: received != \"x\", got \""
                  << received << "\"" << std::endl;
        return false;
    }

    // 11. 通过 FdEvent 删除事件。
    if (!readEvent.unregisterFromReactor()) {
        std::cerr << "[reactor] FAIL: unregisterFromReactor failed" << std::endl;
        return false;
    }
    if (readEvent.isRegistered()) {
        std::cerr << "[reactor] FAIL: event is still registered after unregister" << std::endl;
        return false;
    }

    // 12. 再向 pipe 写端写入 "y"。
    if (write(pipeFds[1], "y", 1) < 0) {
        std::cerr << "[reactor] FAIL: second write to pipe failed" << std::endl;
        return false;
    }

    // 13. 调用 waitOnce(100)，超时 100ms。事件已删除，应超时返回 0。
    nfds = reactor.waitOnce(100);
    if (nfds != 0) {
        std::cerr << "[reactor] FAIL: waitOnce after epollDel returned "
                  << nfds << ", expected 0" << std::endl;
        return false;
    }

    // 14. 验证回调次数没有继续增加。
    if (readCount != 1) {
        std::cerr << "[reactor] FAIL: readCount changed after epollDel, got "
                  << readCount << std::endl;
        return false;
    }

    // 15. 关闭 pipe 两端 fd。
    close(pipeFds[0]);
    close(pipeFds[1]);

    return true;
}

bool testAddTaskWakesLoopAndRunsOnReactorThread()
{
    tinyrpc::Reactor reactor;
    std::atomic<bool> taskDone {false};
    std::thread::id loopThreadId;
    std::thread::id callbackThreadId;

    std::thread loopThread([&]() {
        loopThreadId = std::this_thread::get_id();
        while (!taskDone.load()) {
            reactor.waitOnce(1000);
        }
    });

    std::this_thread::sleep_for(std::chrono::milliseconds(20));
    reactor.addTask([&]() {
        callbackThreadId = std::this_thread::get_id();
        taskDone.store(true);
    });
    loopThread.join();

    if (!taskDone.load()) {
        std::cerr << "[reactor] FAIL: task did not run" << std::endl;
        return false;
    }
    if (callbackThreadId != loopThreadId) {
        std::cerr << "[reactor] FAIL: task did not run on reactor thread" << std::endl;
        return false;
    }

    return true;
}

bool testAddTaskRunsInSubmitOrder()
{
    tinyrpc::Reactor reactor;
    std::vector<int> order;
    std::atomic<bool> done {false};

    std::thread loopThread([&]() {
        while (!done.load()) {
            reactor.waitOnce(1000);
        }
    });

    std::this_thread::sleep_for(std::chrono::milliseconds(20));
    reactor.addTask([&]() {
        order.push_back(1);
    });
    reactor.addTask([&]() {
        order.push_back(2);
    });
    reactor.addTask([&]() {
        order.push_back(3);
        done.store(true);
    });
    loopThread.join();

    if (order != std::vector<int>({1, 2, 3})) {
        std::cerr << "[reactor] FAIL: task order mismatch" << std::endl;
        return false;
    }

    return true;
}

bool testStopWakesBlockedLoop()
{
    tinyrpc::Reactor reactor;
    std::atomic<bool> loopExited {false};

    std::thread loopThread([&]() {
        reactor.loop();
        loopExited.store(true);
    });

    std::this_thread::sleep_for(std::chrono::milliseconds(20));
    reactor.stop();
    loopThread.join();

    if (!loopExited.load()) {
        std::cerr << "[reactor] FAIL: stop did not wake blocked loop" << std::endl;
        return false;
    }

    return true;
}

}

int main()
{
    if (!testFdEventReadCallbackAndDelete()) {
        return 1;
    }
    if (!testAddTaskWakesLoopAndRunsOnReactorThread()) {
        return 1;
    }
    if (!testAddTaskRunsInSubmitOrder()) {
        return 1;
    }
    if (!testStopWakesBlockedLoop()) {
        return 1;
    }

    std::cout << "[reactor] PASS" << std::endl;
    return 0;
}
