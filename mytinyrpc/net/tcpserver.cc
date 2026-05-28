#include "net/tcpserver.h"
#include "net/tcpconnection.h"
#include "net/fdutil.h"
#include "comm/log.h"

#include <cerrno>
#include <string>
#include <sys/epoll.h>
#include <sys/socket.h>
#include <unistd.h>

namespace tinyrpc {

TcpServer::TcpServer(const IPAddress& addr, AbstractCodec::Ptr codec)
    : m_addr(addr),
      m_codec(std::move(codec))
{
    DebugLog("TcpServer constructed on " + m_addr.toString());
}

TcpServer::~TcpServer()
{
    if (m_listenFd != kInvalidSocket) {
        close(m_listenFd);
    }
}

const IPAddress& TcpServer::getLocalAddress() const
{
    return m_addr;
}

bool TcpServer::init()
{
    m_listenFd = socket(AF_INET, SOCK_STREAM, 0);
    if (m_listenFd < 0) {
        ErrorLog("create socket failed");
        return false;
    }

    if (!setReuseAddr(m_listenFd)) {
        return false;
    }

    if (!setNonBlock(m_listenFd)) {
        return false;
    }

    int rt = bind(m_listenFd, m_addr.getSockAddr(), m_addr.getSockLen());
    if (rt != 0) {
        ErrorLog("bind failed");
        return false;
    }

    // 第二个参数是监听队列的上限(backlog)，SOMAXCONN 表示交给系统使用默认的最大值
    rt = listen(m_listenFd, SOMAXCONN);
    if (rt != 0) {
        ErrorLog("listen failed");
        return false;
    }

    InfoLog("TcpServer listen on " + m_addr.toString());
    return true;
}

void TcpServer::start()
{
    InfoLog("TcpServer start on " + m_addr.toString());

    // 将监听 fd 封装为 FdEvent，注册 EPOLLIN 事件到 Reactor。
    // 当有新的客户端连接到达时，Reactor 会触发 acceptLoop() 回调。
    m_listenEvent.setFd(m_listenFd);
    m_listenEvent.addListenEvent(EPOLLIN);
    m_listenEvent.setReadCallback([this]() { acceptLoop(); });

    if (!m_reactor.epollAdd(&m_listenEvent)) {
        ErrorLog("TcpServer add listen event to reactor failed");
        return;
    }

    m_running = true;
    while (m_running) {
        // waitOnce(-1) 表示无限等待，直到有事件发生或被信号中断。
        // 返回 -1 表示 epoll_wait 出错，此时退出事件循环。
        int rt = m_reactor.waitOnce(-1);
        if (rt < 0) {
            ErrorLog("TcpServer reactor waitOnce failed");
            break;
        }
    }
}

void TcpServer::acceptLoop()
{
    // acceptLoop 由 Reactor 在监听 fd 可读时触发。
    // 循环 accept 直到连接队列清空（EAGAIN），充分利用一次事件通知。
    while (true) {
        sockaddr_in clientAddr {};
        socklen_t clientLen = sizeof(clientAddr);

        Socket clientFd = accept(m_listenFd, reinterpret_cast<sockaddr*>(&clientAddr), &clientLen);

        if (clientFd < 0) {
            // EINTR：被信号中断，重试 accept。
            if (errno == EINTR) {
                continue;
            }
            // EAGAIN/EWOULDBLOCK：连接队列已为空，退出循环等待下一次 EPOLLIN。
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                break;
            }
            ErrorLog("accept failed, errno = " + std::to_string(errno));
            break;
        }

        InfoLog("TcpServer accept client fd = " + std::to_string(clientFd));

        if (!setNonBlock(clientFd)) {
            ErrorLog("setNonBlock failed for client fd = " + std::to_string(clientFd));
            close(clientFd);
            continue;
        }

        // 任务二十四：读写均走协程 hook，startConnection 完成 FdEvent 注册 + 启动连接协程。
        auto conn = std::make_shared<TcpConnection>(clientFd, &m_reactor, m_codec);
        conn->setCloseCallback([this](int fd) {
            this->removeConnection(fd);
        });
        conn->startConnection();
        m_connections[clientFd] = conn;
    }
}

void TcpServer::removeConnection(int fd)
{
    m_connections.erase(fd);
}

}
