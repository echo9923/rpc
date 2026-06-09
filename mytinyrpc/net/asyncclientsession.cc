#include "net/asyncclientsession.h"

#include "comm/errorcode.h"
#include "comm/log.h"
#include "net/fdutil.h"
#include "net/fdevent.h"
#include "net/reactor.h"
#include "net/timer.h"

#include <cerrno>
#include <chrono>
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

void AsyncClientSession::shutdownSocket()
{
    if (m_fd != kInvalidSocket) {
        ::shutdown(m_fd, SHUT_RDWR);
    }
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

    // 将当前请求追加编码到 output buffer，交给 flushOutput()/EPOLLOUT 推进。
    // 这里不清空缓冲区，避免覆盖前一个尚未完全写出的异步请求。
    m_connection->encodeClientRequest(request);
    if (!request->m_encodeSucc) {
        m_errorCode = ERROR_FAILED_SERIALIZE;
        m_errorInfo = "TinyPB request encode failed";
        disconnect();
        return false;
    }

    if (!flushOutput()) {
        if (!m_isConnected) {
            return false;
        }
        registerFdEvent();
    }
    return true;
}

void AsyncClientSession::setReadCallback(ReadCallback cb)
{
    m_readCallback = std::move(cb);
}

void AsyncClientSession::setErrorCallback(ErrorCallback cb)
{
    m_errorCallback = std::move(cb);
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
        m_fdEvent.registerToReactor();
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
    while (true) {
        // recv(2) 参数依次为：socket fd、接收缓冲区、最大读取字节数、标志位。
        // MSG_DONTWAIT 让本次读取不阻塞；EAGAIN/EWOULDBLOCK 表示当前批次已读尽。
        ssize_t n = ::recv(m_fd, data, sizeof(data), MSG_DONTWAIT);
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
        if (errno == EAGAIN || errno == EWOULDBLOCK) {
            break;
        }

        m_errorCode = ERROR_TCP_RECV_FAILED;
        m_errorInfo = "recv failed: " + std::string(std::strerror(errno));
        notifyError(m_errorCode, m_errorInfo);
        disconnect();
        return;
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
        notifyError(m_errorCode, m_errorInfo);
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
            notifyError(m_errorCode, m_errorInfo);
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
        notifyError(m_errorCode, m_errorInfo);
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
    m_fdEvent.setReadCallback([this]() {
        handleRead();
    });
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

void AsyncClientSession::notifyError(int errorCode, const std::string& errorInfo)
{
    if (m_errorCallback) {
        m_errorCallback(errorCode, errorInfo);
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

bool AsyncClientSession::recvResponse(const std::string& reqId, TinyPbStruct *response, int timeoutMs)
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

    auto startTime = std::chrono::steady_clock::now();
    int defaultPollMs = (timeoutMs > 0) ? timeoutMs : 5000;

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
            // 计算剩余超时时间，避免快速响应场景下 poll 超过总超时。
            int pollMs = defaultPollMs;
            if (timeoutMs > 0) {
                auto elapsed = std::chrono::steady_clock::now() - startTime;
                int elapsedMs = static_cast<int>(
                    std::chrono::duration_cast<std::chrono::milliseconds>(elapsed).count());
                pollMs = defaultPollMs - elapsedMs;
                if (pollMs <= 0) {
                    m_errorCode = ERROR_TCP_RECV_FAILED;
                    m_errorInfo = "recvResponse timeout";
                    return false;
                }
            }

            struct pollfd pfd;
            pfd.fd = m_fd;
            pfd.events = POLLIN;
            pfd.revents = 0;
            int pr = ::poll(&pfd, 1, pollMs);
            if (pr > 0) {
                continue;
            }
            m_errorCode = ERROR_TCP_RECV_FAILED;
            m_errorInfo = "recvResponse EAGAIN wait failed";
            return false;
        }
        m_errorCode = ERROR_TCP_RECV_FAILED;
        m_errorInfo = "read failed: " + std::string(std::strerror(errno));
        disconnect();
        return false;
    }
}

}
