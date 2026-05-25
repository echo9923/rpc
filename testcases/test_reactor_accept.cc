#include "net/fdevent.h"
#include "net/fdutil.h"
#include "net/netaddress.h"
#include "net/reactor.h"

#include "comm/log.h"

#include <cerrno>
#include <cstring>
#include <string>
#include <sys/epoll.h>
#include <sys/socket.h>
#include <unistd.h>

int main()
{
    // 1. 创建监听 socket
    int listenFd = socket(AF_INET, SOCK_STREAM, 0);
    if (listenFd < 0) {
        ErrorLog("socket failed: " + std::string(std::strerror(errno)));
        return 1;
    }

    // 2. 设置 SO_REUSEADDR 和 O_NONBLOCK
    if (!tinyrpc::setReuseAddr(listenFd)) {
        close(listenFd);
        return 1;
    }
    if (!tinyrpc::setNonBlock(listenFd)) {
        close(listenFd);
        return 1;
    }

    // 3. 绑定到 127.0.0.1:19999
    tinyrpc::IPAddress addr("127.0.0.1", 19999);
    if (bind(listenFd, addr.getSockAddr(), addr.getSockLen()) != 0) {
        ErrorLog("bind failed: " + std::string(std::strerror(errno)));
        close(listenFd);
        return 1;
    }

    // 4. 开始监听
    if (listen(listenFd, SOMAXCONN) != 0) {
        ErrorLog("listen failed: " + std::string(std::strerror(errno)));
        close(listenFd);
        return 1;
    }

    InfoLog("reactor accept demo starting, listen on " + addr.toString());

    // 5. 构造 Reactor
    tinyrpc::Reactor reactor;

    // 6. 构造 FdEvent，监听 EPOLLIN
    tinyrpc::FdEvent listenEvent(listenFd);
    listenEvent.addListenEvent(EPOLLIN);

    // 7. 设置读回调：accept 所有已到达的连接
    // listenFd 已设为非阻塞，在 while 循环中耗尽所有已到达的连接。
    // accept 返回 -1 时分三种情况处理：
    //   EINTR          → 被信号中断，重试
    //   EAGAIN / EWOULDBLOCK → 无更多连接，退出循环
    //   其他错误       → 记录日志后退出
    listenEvent.setReadCallback([listenFd]() {
        while (true) {
            int clientFd = accept(listenFd, nullptr, nullptr);
            if (clientFd >= 0) {
                InfoLog("reactor accept client fd = " + std::to_string(clientFd));
                close(clientFd);
                continue;
            }

            if (errno == EINTR) {
                continue;
            }

            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                break;
            }

            ErrorLog("accept failed: " + std::string(std::strerror(errno)));
            break;
        }
    });

    // 8. 注册 FdEvent 到 Reactor
    if (!reactor.addEvent(&listenEvent)) {
        ErrorLog("addEvent failed for listen fd " + std::to_string(listenFd));
        close(listenFd);
        return 1;
    }

    InfoLog("reactor accept demo started, waiting for connections on " + addr.toString());

    // 9. 主循环：反复调用 waitOnce，-1 表示无限等待
    while (true) {
        reactor.waitOnce(-1);
    }

    close(listenFd);
    return 0;
}
