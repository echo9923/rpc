#include "net/tcpconnection.h"
#include "comm/log.h"

#include <cerrno>
#include <sys/epoll.h>
#include <unistd.h>

namespace tinyrpc {

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
        sendData(result.data);
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

void TcpConnection::handleWrite()
{
    // 循环从 m_outputBuffer 写入数据，处理短写和 EAGAIN。
    while (!m_outputBuffer.empty()) {
        ssize_t n = write(m_fd, m_outputBuffer.data(), m_outputBuffer.size());

        if (n > 0) {
            m_outputBuffer.erase(0, static_cast<size_t>(n));
            continue;
        }

        if (n < 0) {
            if (errno == EINTR) {
                continue;
            }
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                return;
            }
            ErrorLog("handleWrite failed, fd = " + std::to_string(m_fd));
            closeConnection();
            return;
        }

        // n == 0，不应发生但安全处理
        ErrorLog("handleWrite returned 0, fd = " + std::to_string(m_fd));
        closeConnection();
        return;
    }

    // 输出缓冲区已写空，删除 EPOLLOUT，保留 EPOLLIN
    disableWriteEvent();
}

void TcpConnection::sendData(const std::string& data)
{
    if (m_isClosed || data.empty()) {
        return;
    }

    m_outputBuffer.append(data);
    enableWriteEvent();
}

void TcpConnection::enableWriteEvent()
{
    m_fdEvent.addListenEvent(EPOLLOUT);
    m_fdEvent.setWriteCallback([this]() { handleWrite(); });

    m_reactor->modEvent(&m_fdEvent);
}

void TcpConnection::disableWriteEvent()
{
    m_fdEvent.delListenEvent(EPOLLOUT);

    m_reactor->modEvent(&m_fdEvent);
}

void TcpConnection::closeConnection()
{
    if (m_fd < 0) {
        return;
    }

    m_isClosed = true;

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

}
