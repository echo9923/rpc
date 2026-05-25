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
    // 将 client fd 封装为 FdEvent，并设置读写回调。
    // EPOLLIN / EPOLLOUT 的启停统一由 enable/disable*Event 管理。
    if (m_reactor == nullptr) {
        ErrorLog("TcpConnection register failed, reactor is null, fd = " + std::to_string(m_fd));
        return;
    }

    std::weak_ptr<TcpConnection> weakConn = weak_from_this();

    m_fdEvent.setFd(m_fd);
    m_fdEvent.setReactor(m_reactor);
    m_fdEvent.setReadCallback([weakConn]() {
        auto conn = weakConn.lock();
        if (conn) {
            conn->handleRead();
        }
    });
    m_fdEvent.setWriteCallback([weakConn]() {
        auto conn = weakConn.lock();
        if (conn) {
            conn->handleWrite();
        }
    });

    enableReadEvent();

    if (!m_fdEvent.registerToReactor()) {
        ErrorLog("TcpConnection register fd event failed, fd = " + std::to_string(m_fd));
    }
}

void TcpConnection::handleRead()
{
    auto self = shared_from_this();

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

    if (result.status == ReadStatus::Closed) {
        m_closeAfterWrite = true;
        if (m_outputBuffer.getReadableBytes() > 0) {
            disableReadEvent();
            enableWriteEvent();
            return;
        }
        closeWithCallback();
        return;
    }

    if (result.status == ReadStatus::Error) {
        closeWithCallback();
        return;
    }
}

void TcpConnection::handleWrite()
{
    auto self = shared_from_this();

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

    updateEvent();
}

void TcpConnection::disableWriteEvent()
{
    m_fdEvent.delListenEvent(EPOLLOUT);

    updateEvent();
}

void TcpConnection::enableReadEvent()
{
    m_fdEvent.addListenEvent(EPOLLIN);

    updateEvent();
}

void TcpConnection::disableReadEvent()
{
    m_fdEvent.delListenEvent(EPOLLIN);

    updateEvent();
}

void TcpConnection::updateEvent()
{
    if (m_isClosed) {
        return;
    }

    m_fdEvent.updateToReactor();
}

void TcpConnection::closeConnection()
{
    if (m_isClosed || m_fd < 0) {
        return;
    }

    m_isClosed = true;

    InfoLog("TcpConnection close, fd = " + std::to_string(m_fd));

    // 先删除事件再关闭 fd，避免 epoll 仍持有已关闭的 fd
    m_fdEvent.unregisterFromReactor();
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
