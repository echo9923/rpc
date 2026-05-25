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
    if (m_closeAfterWrite) {
        return;
    }

    ReadResult result = readData();

    if (result.status == ReadStatus::Data) {
        sendData(result.data);
        return;
    }

    if (result.status == ReadStatus::Again) {
        return;
    }

    if (result.status == ReadStatus::Closed || result.status == ReadStatus::Error) {
        m_closeAfterWrite = true;
        enableWriteEvent();
        return;
    }
}

void TcpConnection::handleWrite()
{
    // 循环从 TcpBuffer 可读区域写数据到 socket，处理短写和 EAGAIN。
    // write(fd, buf, count) 将 buf 指向的 count 字节写入 fd 对应的 socket 发送缓冲区。
    // 返回值：>0 实际写入字节数；0 不应发生；<0 且 errno=EAGAIN 表示发送缓冲区满需等待。
    while (m_outputBuffer.getReadableBytes() > 0) {
        ssize_t n = write(
            m_fd,
            m_outputBuffer.getReadPtr(),
            m_outputBuffer.getReadableBytes()
        );

        if (n > 0) {
            m_outputBuffer.retrieve(static_cast<size_t>(n));
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
            closeWithCallback();
            return;
        }

        // n == 0，不应发生但安全处理
        ErrorLog("handleWrite returned 0, fd = " + std::to_string(m_fd));
        closeWithCallback();
        return;
    }

    // 输出缓冲区已写空，删除 EPOLLOUT，保留 EPOLLIN
    disableWriteEvent();

    if (m_closeAfterWrite) {
        closeWithCallback();
    }
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
    if (m_isClosed || m_fd < 0) {
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

void TcpConnection::closeWithCallback()
{
    Socket closedFd = m_fd;
    closeConnection();

    if (m_closeCallback) {
        m_closeCallback(closedFd);
    }
}

ReadResult TcpConnection::readData()
{
    char buffer[1024] = {0};

    // read(fd, buf, count) 从 fd 对应的 socket 接收缓冲区读取最多 count 字节到 buf。
    // 返回值：>0 实际读取字节数；0 表示对端关闭；<0 且 errno=EAGAIN 表示当前无可读数据。
    ssize_t readBytes = read(m_fd, buffer, sizeof(buffer));

    if (readBytes < 0) {
        // EAGAIN / EWOULDBLOCK：非阻塞 socket 上当前无数据可读，不是错误，稍后重试即可。
        // EINTR：在读取到数据前被信号中断，同样不是错误，返回 Again 让上层在下次事件循环中重新读取。
        if (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR) {
            return {ReadStatus::Again, {}};
        }
        ErrorLog(
            "read failed, fd = " +
            std::to_string(m_fd)
        );
        return {ReadStatus::Error, {}};
    }

    if (readBytes == 0) {
        InfoLog("client closed, fd = " + std::to_string(m_fd));
        return {ReadStatus::Closed, {}};
    }

    m_inputBuffer.append(buffer, static_cast<size_t>(readBytes));

    return {ReadStatus::Data, m_inputBuffer.retrieveAllAsString()};
}

}
