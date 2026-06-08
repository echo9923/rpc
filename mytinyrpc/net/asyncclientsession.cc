#include "net/asyncclientsession.h"

#include "comm/errorcode.h"
#include "comm/log.h"
#include "net/fdutil.h"
#include "net/fdevent.h"
#include "net/reactor.h"
#include "net/timer.h"

#include <cerrno>
#include <poll.h>
#include <cstring>
#include <sys/epoll.h>
#include <sys/socket.h>
#include <unistd.h>

namespace tinyrpc {

AsyncClientSession::AsyncClientSession(const IPAddress& peerAddr)
    : m_peerAddr(peerAddr),
      m_codec(std::make_shared<TinyPbCodec>())
{
}

AsyncClientSession::~AsyncClientSession()
{
    disconnect();
}

const IPAddress& AsyncClientSession::getPeerAddress() const
{
    return m_peerAddr;
}

Socket AsyncClientSession::getFd() const
{
    return m_fd;
}

bool AsyncClientSession::isConnected() const
{
    return m_isConnected;
}

std::string AsyncClientSession::getErrorInfo() const
{
    return m_errorInfo;
}

int AsyncClientSession::getErrorCode() const
{
    return m_errorCode;
}

bool AsyncClientSession::connect()
{
    if (m_isConnected) {
        return true;
    }

    // socket(2) 参数：AF_INET = IPv4, SOCK_STREAM = TCP, 0 = 自动协议。
    m_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (m_fd < 0) {
        m_errorCode = ERROR_TCP_CONNECT_FAILED;
        m_errorInfo = "socket() failed: " + std::string(std::strerror(errno));
        return false;
    }

    // connect(2) 向 m_peerAddr 发起 TCP 三次握手。
    // 阻塞模式下，成功返回 0；失败返回 -1，常见 errno：
    // ECONNREFUSED（无人监听）、ETIMEDOUT（超时）、ENETUNREACH（不可达）。
    int rt = ::connect(m_fd, m_peerAddr.getSockAddr(), m_peerAddr.getSockLen());
    if (rt != 0) {
        m_errorCode = ERROR_TCP_CONNECT_FAILED;
        m_errorInfo = "connect to " + m_peerAddr.toString()
            + " failed: " + std::string(std::strerror(errno));
        ::close(m_fd);
        m_fd = kInvalidSocket;
        return false;
    }

    // connect() succeeded: switch to non-blocking for EPOLLIN/EPOLLOUT.
    setNonBlock(m_fd);

    m_reactor = Reactor::getCurrentReactor();

    m_connection = std::make_shared<TcpConnection>(
        m_fd,
        nullptr,
        TcpConnectionType::ClientConnection,
        m_peerAddr,
        m_codec);

    m_isConnected = true;
    m_errorCode = 0;
    m_errorInfo.clear();

    if (m_readCallback) {
        startAsyncRead();
    }
    return true;
}

void AsyncClientSession::disconnect()
{
    unregisterFdEvent();

    if (m_connection != nullptr) {
        m_connection->closeConnection();
        m_connection.reset();
    } else if (m_fd != kInvalidSocket) {
        ::close(m_fd);
    }

    m_fd = kInvalidSocket;
    m_isConnected = false;
    m_reactor = nullptr;
}

bool AsyncClientSession::sendRequest(TinyPbStruct *request)
{
    if (!m_isConnected || m_connection == nullptr || request == nullptr) {
        m_errorCode = ERROR_TCP_SEND_FAILED;
        m_errorInfo = "session not connected or invalid argument";
        return false;
    }

    // 清空上一次残留的输出数据，然后编码当前请求到输出缓冲区。
    m_connection->getOutputBuffer()->retrieveAll();
    m_connection->encodeClientRequest(request);
    if (!request->m_encodeSucc) {
        m_errorCode = ERROR_FAILED_SERIALIZE;
        m_errorInfo = "TinyPB request encode failed";
        disconnect();
        return false;
    }

    // Non-blocking send loop: retries on EINTR/EAGAIN.
    // EAGAIN uses epoll_wait to block until the socket is writable,
    // keeping the IOThread responsive to other events.
    TcpBuffer *outBuffer = m_connection->getOutputBuffer();
    size_t written = 0;
    size_t total = outBuffer->getReadableBytes();

    while (written < total) {
        ssize_t n = ::send(m_fd, outBuffer->getReadPtr() + written,
                           total - written, MSG_NOSIGNAL);
        if (n > 0) {
            written += static_cast<size_t>(n);
            continue;
        }
        if (n == 0) {
            m_errorCode = ERROR_TCP_SEND_FAILED;
            m_errorInfo = "send returned zero";
            disconnect();
            return false;
        }
        if (errno == EINTR) {
            continue;
        }
        if (errno == EAGAIN || errno == EWOULDBLOCK) {
            struct pollfd pfd;
            pfd.fd = m_fd;
            pfd.events = POLLOUT;
            pfd.revents = 0;
            int pr = ::poll(&pfd, 1, 5000);
            if (pr > 0) {
                continue;
            }
            m_errorCode = ERROR_TCP_SEND_FAILED;
            m_errorInfo = "send EAGAIN wait failed";
            disconnect();
            return false;
        }
        m_errorCode = ERROR_TCP_SEND_FAILED;
        m_errorInfo = "send failed: " + std::string(std::strerror(errno));
        disconnect();
        return false;
    }

    outBuffer->retrieveAll();
    return true;
}

void AsyncClientSession::setReadCallback(ReadCallback cb)
{
    m_readCallback = std::move(cb);
}

void AsyncClientSession::startAsyncRead()
{
    if (m_reactor == nullptr || m_fd == kInvalidSocket) {
        return;
    }

    m_fdEvent.setFd(m_fd);
    m_fdEvent.setReactor(m_reactor);
    m_fdEvent.setReadCallback([this]() {
        handleRead();
    });
    m_fdEvent.addListenEvent(EPOLLIN);

    if (!m_fdEvent.isRegistered()) {
        bool ok = m_fdEvent.registerToReactor();
    } else {
        m_fdEvent.updateToReactor();
    }
}

void AsyncClientSession::handleRead()
{
    if (!m_isConnected || m_connection == nullptr) {
        return;
    }

    bool peerClosed = false;
    char data[4096];
    int readIterations = 0;
    while (true) {
        ssize_t n = ::recv(m_fd, data, sizeof(data), MSG_DONTWAIT);
        ++readIterations;
        if (n > 0) {
            m_connection->appendClientInput(data, static_cast<size_t>(n));
            continue;
        }
        if (n == 0) {
            peerClosed = true;
            break;
        }
        if (errno == EINTR) {
            continue;
        }
        break;
    }

    // Process any buffered responses before handling peer close.
    m_connection->parseClientResponses();


    TinyPbStruct response;
    while (m_connection->popClientResponse(&response)) {
        if (m_readCallback) {
            m_readCallback(response);
        }
    }

    if (peerClosed) {
        m_errorCode = ERROR_TCP_RECV_FAILED;
        m_errorInfo = "peer closed connection";
        disconnect();
    }
}

bool AsyncClientSession::flushOutput()
{
    if (!m_isConnected || m_connection == nullptr) {
        return false;
    }

    TcpBuffer *outBuffer = m_connection->getOutputBuffer();

    while (outBuffer->getReadableBytes() > 0) {
        // send(2) 参数依次为：socket fd、待写缓冲区、待写字节数、标志位。
        // MSG_NOSIGNAL 防止对端关闭时产生 SIGPIPE。
        ssize_t n = ::send(m_fd, outBuffer->getReadPtr(),
                           outBuffer->getReadableBytes(), MSG_NOSIGNAL);
        if (n > 0) {
            outBuffer->retrieve(static_cast<size_t>(n));
            continue;
        }

        if (n == 0) {
            m_errorCode = ERROR_TCP_SEND_FAILED;
            m_errorInfo = "send returned zero";
            disconnect();
            return false;
        }

        if (errno == EINTR) {
            continue;
        }

        if (errno == EAGAIN || errno == EWOULDBLOCK) {
            // 发送缓冲区暂满，返回 false 让调用方注册 EPOLLOUT。
            return false;
        }

        m_errorCode = ERROR_TCP_SEND_FAILED;
        m_errorInfo = "send failed: " + std::string(std::strerror(errno));
        disconnect();
        return false;
    }

    // Output buffer fully written; remove EPOLLOUT only, keep EPOLLIN active.
    if (m_fdEvent.isRegistered()) {
        m_fdEvent.delListenEvent(EPOLLOUT);
        if (m_fdEvent.getListenEvents() == 0) {
            m_fdEvent.unregisterFromReactor();
        } else {
            m_fdEvent.updateToReactor();
        }
    }
    return true;
}

void AsyncClientSession::registerFdEvent()
{
    if (m_reactor == nullptr || m_fd == kInvalidSocket) {
        return;
    }

    m_fdEvent.setFd(m_fd);
    m_fdEvent.setReactor(m_reactor);
    m_fdEvent.setWriteCallback([this]() {
        flushOutput();
    });
    m_fdEvent.addListenEvent(EPOLLOUT);

    if (!m_fdEvent.isRegistered()) {
        m_fdEvent.registerToReactor();
    } else {
        m_fdEvent.updateToReactor();
    }
}

void AsyncClientSession::unregisterFdEvent()
{
    if (m_fdEvent.isRegistered()) {
        m_fdEvent.delListenEvent(EPOLLOUT);
        m_fdEvent.delListenEvent(EPOLLIN);
        m_fdEvent.unregisterFromReactor();
    }
    m_fdEvent.setFd(kInvalidSocket);
}

bool AsyncClientSession::recvResponse(const std::string& reqId, TinyPbStruct *response)
{
    if (!m_isConnected || m_connection == nullptr || response == nullptr) {
        m_errorCode = ERROR_TCP_RECV_FAILED;
        m_errorInfo = "session not connected or invalid argument";
        return false;
    }

    response->m_reqId = reqId;

    // 先检查 TcpConnection 内部是否已缓存匹配响应。
    if (m_connection->getClientResponse(reqId, response)) {
        return true;
    }

    // 循环读取 socket 数据，解码并匹配 reqId。
    while (true) {
        m_connection->parseClientResponses();
        if (m_connection->getClientResponse(reqId, response)) {
            return true;
        }

        // 从 socket 读取一批字节到 input buffer。
        char data[1024];
        ssize_t n = ::recv(m_fd, data, sizeof(data), MSG_DONTWAIT);
        if (n > 0) {
            m_connection->appendClientInput(data, static_cast<size_t>(n));
            continue;
        }
        if (n == 0) {
            m_errorCode = ERROR_TCP_RECV_FAILED;
            m_errorInfo = "peer closed connection";
            disconnect();
            return false;
        }
        if (errno == EINTR) {
            continue;
        }
        if (errno == EAGAIN || errno == EWOULDBLOCK) {
            struct pollfd pfd;
            pfd.fd = m_fd;
            pfd.events = POLLIN;
            pfd.revents = 0;
            int pr = ::poll(&pfd, 1, 5000);
            if (pr > 0) {
                continue;
            }
            m_errorCode = ERROR_TCP_RECV_FAILED;
            m_errorInfo = "recvResponse EAGAIN wait failed";
            disconnect();
            return false;
        }
        m_errorCode = ERROR_TCP_RECV_FAILED;
        m_errorInfo = "read failed: " + std::string(std::strerror(errno));
        disconnect();
        return false;
    }
}

}
