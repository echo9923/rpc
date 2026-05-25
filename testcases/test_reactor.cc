#include "net/fdevent.h"
#include "net/fdutil.h"
#include "net/reactor.h"

#include <sys/epoll.h>
#include <unistd.h>

#include <iostream>
#include <string>

int main()
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
        return 1;
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

    // 7. 调用 addEvent() 注册 FdEvent。
    if (!reactor.addEvent(&readEvent)) {
        std::cerr << "[reactor] FAIL: addEvent failed" << std::endl;
        return 1;
    }

    // 8. 向 pipe 写端写入 "x"。
    if (write(pipeFds[1], "x", 1) < 0) {
        std::cerr << "[reactor] FAIL: write to pipe failed" << std::endl;
        return 1;
    }

    // 9. 调用 waitOnce(1000)，超时 1 秒。
    int nfds = reactor.waitOnce(1000);
    if (nfds <= 0) {
        std::cerr << "[reactor] FAIL: waitOnce returned " << nfds
                  << ", expected > 0" << std::endl;
        return 1;
    }

    // 10. 验证读回调被调用一次，读取内容是 "x"。
    if (readCount != 1) {
        std::cerr << "[reactor] FAIL: readCount != 1, got " << readCount
                  << std::endl;
        return 1;
    }
    if (received != "x") {
        std::cerr << "[reactor] FAIL: received != \"x\", got \""
                  << received << "\"" << std::endl;
        return 1;
    }

    // 11. 调用 delEvent() 删除事件。
    if (!reactor.delEvent(&readEvent)) {
        std::cerr << "[reactor] FAIL: delEvent failed" << std::endl;
        return 1;
    }

    // 12. 再向 pipe 写端写入 "y"。
    if (write(pipeFds[1], "y", 1) < 0) {
        std::cerr << "[reactor] FAIL: second write to pipe failed" << std::endl;
        return 1;
    }

    // 13. 调用 waitOnce(100)，超时 100ms。事件已删除，应超时返回 0。
    nfds = reactor.waitOnce(100);
    if (nfds != 0) {
        std::cerr << "[reactor] FAIL: waitOnce after delEvent returned "
                  << nfds << ", expected 0" << std::endl;
        return 1;
    }

    // 14. 验证回调次数没有继续增加。
    if (readCount != 1) {
        std::cerr << "[reactor] FAIL: readCount changed after delEvent, got "
                  << readCount << std::endl;
        return 1;
    }

    // 15. 关闭 pipe 两端 fd。
    close(pipeFds[0]);
    close(pipeFds[1]);

    std::cout << "[reactor] PASS" << std::endl;
    return 0;
}
