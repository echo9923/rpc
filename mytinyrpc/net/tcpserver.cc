#include "net/tcpserver.h"
#include "net/tcpconnection.h"
#include "net/fdutil.h"
#include "comm/log.h"

#include <cerrno>
#include <chrono>
#include <sys/socket.h>
#include <string>
#include <thread>
#include <unistd.h>

namespace tinyrpc {

namespace {

void sleepBriefly()
{
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
}

}

TcpServer::TcpServer(const IPAddress& addr)
    : m_addr(addr)
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
    InfoLog("TcpServer start accept loop on " + m_addr.toString());
    acceptLoop();
}

void TcpServer::acceptLoop()
{
    while (true) {
        sockaddr_in clientAddr {};
        socklen_t clientLen = sizeof(clientAddr);

        Socket clientFd = accept(m_listenFd, reinterpret_cast<sockaddr*>(&clientAddr), &clientLen);

        if (clientFd < 0) {
            // 非阻塞监听下，连接队列暂时为空时会返回 EAGAIN/EWOULDBLOCK，稍等后继续轮询。
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                sleepBriefly();
                continue;
            }
            ErrorLog("accept failed");
            continue;
        }

        InfoLog("TcpServer accept client fd = " + std::to_string(clientFd));

        if (!setNonBlock(clientFd)) {
            ErrorLog("setNonBlock failed for client fd = " + std::to_string(clientFd));
            close(clientFd);
            continue;
        }

        TcpConnection conn(clientFd);
        conn.handle();
    }
}

}
