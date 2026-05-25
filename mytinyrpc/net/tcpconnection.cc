#include "net/tcpconnection.h"
#include "comm/log.h"

#include <cerrno>
#include <chrono>
#include <sys/epoll.h>
#include <thread>
#include <unistd.h>

namespace tinyrpc {

namespace {

void sleepBriefly()
{
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
}

}

TcpConnection::TcpConnection(Socket fd, Reactor *reactor)
    : m_fd(fd),
      m_reactor(reactor)
{
}

TcpConnection::~TcpConnection()
{
    closeConnection();
}

Socket TcpConnection::getFd() const
{
    return m_fd;
}

void TcpConnection::registerToReactor()
{
    // 将 client fd 封装为 FdEvent，注册 EPOLLIN 事件到 Reactor。
    // 当客户端发送数据时，Reactor 会触发 handleRead() 回调。
    m_fdEvent.setFd(m_fd);
    m_fdEvent.addListenEvent(EPOLLIN);
    m_fdEvent.setReadCallback([this]() { handleRead(); });

    m_reactor->addEvent(&m_fdEvent);
}

void TcpConnection::handleRead()
{
    ReadResult result = readData();

    if (result.status == ReadStatus::Data) {
        writeData(result.data);
        return;
    }

    if (result.status == ReadStatus::Again) {
        return;
    }

    // Closed 或 Error：关闭连接
    Socket closedFd = m_fd;
    closeConnection();

    if (m_closeCallback) {
        m_closeCallback(closedFd);
    }
}

void TcpConnection::closeConnection()
{
    if (m_fd < 0) {
        return;
    }

    InfoLog("TcpConnection close, fd = " + std::to_string(m_fd));

    // 先删除事件再关闭 fd，避免 epoll 仍持有已关闭的 fd
    m_reactor->delEvent(&m_fdEvent);
    close(m_fd);
    m_fd = -1;
}

void TcpConnection::setCloseCallback(std::function<void(int)> cb)
{
    m_closeCallback = std::move(cb);
}

ReadResult TcpConnection::readData()
{
    char buffer[1024] = {0};

    ssize_t n = read(m_fd, buffer, sizeof(buffer) - 1);

    if (n < 0) {
        if (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR) {
            return {ReadStatus::Again, {}};
        }
        ErrorLog(
            "read failed, fd = " +
            std::to_string(m_fd)
        );
        return {ReadStatus::Error, {}};
    }

    if (n == 0) {
        InfoLog("client closed, fd = " + std::to_string(m_fd));
        return {ReadStatus::Closed, {}};
    }

    return {ReadStatus::Data, std::string(buffer, static_cast<size_t>(n))};
}

bool TcpConnection::writeData(const std::string& data)
{
    size_t totalWritten = 0;

    while (totalWritten < data.size()) {
        ssize_t n = write(
            m_fd,
            data.data() + totalWritten,
            data.size() - totalWritten
        );

        if (n < 0) {
            if (errno == EINTR) {
                continue;
            }
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                sleepBriefly();
                continue;
            }
            ErrorLog(
                "write failed, fd = " +
                std::to_string(m_fd)
            );
            return false;
        }

        if (n == 0) {
            ErrorLog("write returned 0, fd = " + std::to_string(m_fd));
            return false;
        }

        totalWritten += static_cast<size_t>(n);
    }

    InfoLog(
        "TcpConnection write to fd = " +
        std::to_string(m_fd) +
        ", bytes = " +
        std::to_string(totalWritten)
    );

    return true;
}

}
