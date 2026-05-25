#include "mytinyrpc/net/netaddress.h"
#include "mytinyrpc/net/fdutil.h"
#include "mytinyrpc/comm/log.h"

#include <arpa/inet.h>
#include <cerrno>
#include <cstring>
#include <string>
#include <sys/epoll.h>
#include <sys/socket.h>
#include <unistd.h>

namespace tinyrpc {
namespace {

constexpr int kMaxEvents = 64;

int createAndBindSocket(const IPAddress &addr)
{
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) {
        ErrorLog("create socket failed");
        return -1;
    }

    if (!setReuseAddr(fd)) {
        close(fd);
        return -1;
    }

    if (!setNonBlock(fd)) {
        close(fd);
        return -1;
    }

    if (bind(fd, addr.getSockAddr(), addr.getSockLen()) != 0) {
        ErrorLog("bind failed: " + std::string(strerror(errno)));
        close(fd);
        return -1;
    }

    if (listen(fd, SOMAXCONN) != 0) {
        ErrorLog("listen failed: " + std::string(strerror(errno)));
        close(fd);
        return -1;
    }

    return fd;
}

int createEpollFd()
{
    int epfd = epoll_create1(0);   // 0 = 无额外标志，等价于 epoll_create()
    if (epfd < 0) {
        ErrorLog("epoll_create1 failed: " + std::string(strerror(errno)));
    }
    return epfd;
}

bool addListenFdToEpoll(int epfd, int listenFd)
{
    epoll_event ev {};
    ev.events   = EPOLLIN;    // 监听"可读"事件：有新连接到来时触发
    ev.data.fd  = listenFd;   // 保存 fd，epoll_wait 返回时通过 data.fd 识别

    // 将 listenFd 注册到 epoll 实例中
    if (epoll_ctl(epfd, EPOLL_CTL_ADD, listenFd, &ev) != 0) {
        ErrorLog("epoll_ctl add listen fd failed: " + std::string(strerror(errno)));
        return false;
    }
    return true;
}

void acceptClients(int listenFd)
{
    // listenFd 是非阻塞的，accept 会耗尽所有已到达的连接
    // 当返回 EAGAIN/EWOULDBLOCK 时说明已无更多连接，退出循环
    while (true) {
        sockaddr_in clientAddr {};
        socklen_t clientLen = sizeof(clientAddr);

        int clientFd = accept(listenFd, reinterpret_cast<sockaddr *>(&clientAddr), &clientLen);

        if (clientFd < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                // 非阻塞模式下无新连接可接受，所有连接已处理完毕
                break;
            }
            if (errno == EINTR) {
                // 被信号中断，重试 accept
                continue;
            }
            // 其他不可恢复的错误，记录日志后退出
            ErrorLog("accept failed: " + std::string(strerror(errno)));
            break;
        }

        InfoLog("epoll accept client fd = " + std::to_string(clientFd));

        // 本 demo 仅演示 epoll accept，收到连接后立即关闭
        close(clientFd);
    }
}

} // anonymous namespace
} // namespace tinyrpc

int main()
{
    using namespace tinyrpc;

    IPAddress addr("127.0.0.1", 19999);

    InfoLog("epoll demo starting, listen on " + addr.toString());

    int listenFd = createAndBindSocket(addr);
    if (listenFd < 0) {
        return 1;
    }

    int epfd = createEpollFd();
    if (epfd < 0) {
        close(listenFd);
        return 1;
    }

    if (!addListenFdToEpoll(epfd, listenFd)) {
        close(listenFd);
        close(epfd);
        return 1;
    }

    InfoLog("epoll demo started, waiting for connections on " + addr.toString());

    epoll_event events[kMaxEvents];

    while (true) {
        // epoll_wait 参数说明：
        //   epfd      — epoll 实例的文件描述符
        //   events    — 输出参数，用于接收就绪事件的 epoll_event 数组
        //   kMaxEvents— events 数组容量，本次调用最多返回的事件数
        //   -1        — 超时时间（毫秒），-1 表示无限阻塞直到有事件发生
        int n = epoll_wait(epfd, events, kMaxEvents, -1);

        if (n < 0) {
            // EINTR：被信号中断，无需视为错误，重试即可
            if (errno == EINTR) {
                continue;
            }
            // 其他错误（如 EFAULT、EBADF 等），记录日志后退出循环
            ErrorLog("epoll_wait failed: " + std::string(strerror(errno)));
            break;
        }

        // n > 0：遍历所有就绪事件，events[0..n-1] 为有效数据
        for (int i = 0; i < n; ++i) {
            // 判断就绪的 fd 是否为监听套接字
            if (events[i].data.fd == listenFd) {
                InfoLog("epoll event: listen fd readable");
                acceptClients(listenFd);
            }
        }
    }

    close(listenFd);
    close(epfd);

    return 0;
}
