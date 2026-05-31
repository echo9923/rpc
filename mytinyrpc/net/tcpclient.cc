#include "net/tcpclient.h"
#include "comm/errorcode.h"
#include "comm/log.h"
#include "net/fdutil.h"

#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>

#include <chrono>
#include <thread>

namespace tinyrpc {

TcpClient::TcpClient(const IPAddress& peerAddr)
    : m_peerAddr(peerAddr)
{
    DebugLog("TcpClient constructed, peer = " + m_peerAddr.toString());
}

TcpClient::~TcpClient()
{
    closeConnection();
}

const IPAddress& TcpClient::getPeerAddress() const
{
    return m_peerAddr;
}

Socket TcpClient::getFd() const
{
    return m_fd;
}

bool TcpClient::isConnected() const
{
    return m_isConnected;
}

std::string TcpClient::getErrorInfo() const
{
    if (!m_errorInfo.empty()) {
        return m_errorInfo;
    }
    if (m_errorCode == 0) {
        return "";
    }
    return std::strerror(m_errorCode);
}

int TcpClient::getErrorCode() const
{
    return m_errorCode;
}

void TcpClient::setTimeout(int timeoutMs)
{
    m_timeoutMs = timeoutMs > 0 ? timeoutMs : 0;
}

int TcpClient::getTimeout() const
{
    return m_timeoutMs;
}

void TcpClient::setConnectRetry(int retryCount, int retryIntervalMs)
{
    m_connectRetryCount = retryCount > 0 ? retryCount : 0;
    m_connectRetryIntervalMs = retryIntervalMs > 0 ? retryIntervalMs : 0;
}

bool TcpClient::connectServer()
{
    // 已经连接则直接返回成功
    if (m_isConnected) {
        return true;
    }

    int maxAttempts = m_connectRetryCount + 1;
    for (int attempt = 1; attempt <= maxAttempts; ++attempt) {
        if (connectOnce()) {
            return true;
        }

        std::string lastError = getErrorInfo();
        if (attempt >= maxAttempts) {
            m_errorInfo = "connect failed after " + std::to_string(maxAttempts)
                + " attempt(s): " + lastError;
            return false;
        }

        DebugLog("TcpClient connect attempt " + std::to_string(attempt)
                 + " failed, retry after " + std::to_string(m_connectRetryIntervalMs)
                 + " ms, peer = " + m_peerAddr.toString());
        if (m_connectRetryIntervalMs > 0) {
            std::this_thread::sleep_for(std::chrono::milliseconds(m_connectRetryIntervalMs));
        }
    }

    return false;
}

bool TcpClient::connectOnce()
{
    // AF_INET: IPv4 协议族
    // SOCK_STREAM: 面向连接的 TCP 字节流
    // 0: 协议自动选择（TCP）
    m_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (m_fd < 0) {
        m_errorCode = errno;
        m_errorInfo.clear();
        ErrorLog("TcpClient socket() failed, errno = " + std::to_string(m_errorCode));
        return false;
    }

    if (m_timeoutMs > 0 && !setNonBlock(m_fd)) {
        m_errorCode = ERROR_TCP_CONNECT_FAILED;
        m_errorInfo = "set nonblock failed before connect";
        close(m_fd);
        m_fd = kInvalidSocket;
        return false;
    }

    // connect(2)：向 m_peerAddr 发起 TCP 三次握手。
    // 参数依次为：socket fd、对端地址（sockaddr*）、地址长度。
    int rt = connect(m_fd, m_peerAddr.getSockAddr(), m_peerAddr.getSockLen());
    if (rt != 0) {
        if (m_timeoutMs > 0 && errno == EINPROGRESS) {
            if (!waitFdEvent(POLLOUT, "connect", ERROR_TCP_TIMEOUT)) {
                close(m_fd);
                m_fd = kInvalidSocket;
                return false;
            }

            int socketError = 0;
            socklen_t len = sizeof(socketError);
            // getsockopt(SO_ERROR) 读取非阻塞 connect 的最终结果；
            // socketError 为 0 表示连接建立成功，否则为具体 errno。
            rt = getsockopt(m_fd, SOL_SOCKET, SO_ERROR, &socketError, &len);
            if (rt != 0 || socketError != 0) {
                m_errorCode = ERROR_TCP_CONNECT_FAILED;
                m_errorInfo = "connect failed: "
                    + std::string(std::strerror(rt != 0 ? errno : socketError));
                ErrorLog("TcpClient connect() to " + m_peerAddr.toString()
                         + " failed, error = " + m_errorInfo);
                close(m_fd);
                m_fd = kInvalidSocket;
                return false;
            }
        } else {
            m_errorCode = ERROR_TCP_CONNECT_FAILED;
            m_errorInfo = "connect failed: " + std::string(std::strerror(errno));
            ErrorLog("TcpClient connect() to " + m_peerAddr.toString()
                     + " failed, errno = " + std::to_string(errno));
            close(m_fd);
            m_fd = kInvalidSocket;
            return false;
        }
    }

    m_isConnected = true;
    m_errorCode = 0;
    m_errorInfo.clear();
    InfoLog("TcpClient connected to " + m_peerAddr.toString()
            + ", fd = " + std::to_string(m_fd));
    return true;
}

void TcpClient::closeConnection()
{
    if (m_fd != kInvalidSocket) {
        close(m_fd);
        DebugLog("TcpClient closed fd = " + std::to_string(m_fd));
        m_fd = kInvalidSocket;
    }
    m_isConnected = false;
}

bool TcpClient::sendTinyPbRequest(TinyPbStruct *request)
{
    if (request == nullptr) {
        m_errorCode = 0;
        m_errorInfo = "TinyPB request is null";
        return false;
    }

    if (!m_isConnected && !connectServer()) {
        return false;
    }

    TcpBuffer outBuffer(256);
    TinyPbCodec codec;
    codec.encode(&outBuffer, request);
    if (!request->m_encodeSucc) {
        m_errorCode = 0;
        m_errorInfo = "TinyPB request encode failed";
        return false;
    }

    return writeAll(outBuffer.getReadPtr(), outBuffer.getReadableBytes());
}

bool TcpClient::recvTinyPbResponse(TinyPbStruct *response)
{
    if (response == nullptr) {
        m_errorCode = 0;
        m_errorInfo = "TinyPB response is null";
        return false;
    }

    if (!m_isConnected) {
        m_errorCode = 0;
        m_errorInfo = "TcpClient is not connected";
        return false;
    }

    TcpBuffer inBuffer(256);
    TinyPbCodec codec;

    while (true) {
        codec.decode(&inBuffer, response);
        if (response->m_decodeSucc) {
            m_errorCode = 0;
            m_errorInfo.clear();
            return true;
        }

        if (!readSomeToBuffer(&inBuffer)) {
            return false;
        }

        if (inBuffer.getReadableBytes() > static_cast<size_t>(kTinyPbMaxPackageLength)) {
            m_errorCode = 0;
            m_errorInfo = "TinyPB response exceeds max package length";
            return false;
        }
    }
}

bool TcpClient::sendAndRecvTinyPb(TinyPbStruct *request, TinyPbStruct *response)
{
    if (!sendTinyPbRequest(request)) {
        return false;
    }
    return recvTinyPbResponse(response);
}

bool TcpClient::writeAll(const char *data, size_t len)
{
    if (data == nullptr && len > 0) {
        m_errorCode = 0;
        m_errorInfo = "write data is null";
        return false;
    }

    size_t written = 0;
    while (written < len) {
        if (!waitFdEvent(POLLOUT, "write", ERROR_TCP_TIMEOUT)) {
            return false;
        }

        // send(2) 参数依次为：socket fd、待写缓冲区地址、待写字节数、发送标志。
        // MSG_NOSIGNAL 表示对端已关闭时不向进程发送 SIGPIPE，而是让 send 返回 -1/EPIPE。
        ssize_t n = send(m_fd, data + written, len - written, MSG_NOSIGNAL);
        if (n > 0) {
            written += static_cast<size_t>(n);
            continue;
        }

        if (n == 0) {
            m_errorCode = ERROR_TCP_SEND_FAILED;
            m_errorInfo = "write returned zero";
            return false;
        }

        if (errno == EINTR) {
            continue;
        }

        m_errorCode = ERROR_TCP_SEND_FAILED;
        if (errno == EAGAIN || errno == EWOULDBLOCK) {
            m_errorInfo = "socket is temporarily not writable";
        } else {
            m_errorInfo = "write failed: " + std::string(std::strerror(errno));
        }
        ErrorLog("TcpClient write() failed, errno = " + std::to_string(errno));
        return false;
    }

    m_errorCode = 0;
    m_errorInfo.clear();
    return true;
}

bool TcpClient::readSomeToBuffer(TcpBuffer *buffer)
{
    if (buffer == nullptr) {
        m_errorCode = 0;
        m_errorInfo = "read buffer is null";
        return false;
    }

    char data[1024];
    while (true) {
        if (!waitFdEvent(POLLIN, "read", ERROR_TCP_TIMEOUT)) {
            return false;
        }

        // read(2) 参数依次为：socket fd、接收缓冲区地址、最大读取字节数。
        ssize_t n = read(m_fd, data, sizeof(data));
        if (n > 0) {
            buffer->append(data, static_cast<size_t>(n));
            m_errorCode = 0;
            m_errorInfo.clear();
            return true;
        }

        if (n == 0) {
            m_errorCode = ERROR_TCP_RECV_FAILED;
            m_errorInfo = "peer closed connection";
            return false;
        }

        if (errno == EINTR) {
            continue;
        }

        m_errorCode = ERROR_TCP_RECV_FAILED;
        if (errno == EAGAIN || errno == EWOULDBLOCK) {
            m_errorInfo = "socket is temporarily not readable";
        } else {
            m_errorInfo = "read failed: " + std::string(std::strerror(errno));
        }
        ErrorLog("TcpClient read() failed, errno = " + std::to_string(errno));
        return false;
    }
}

bool TcpClient::waitFdEvent(short event, const std::string& operation, int timeoutErrorCode)
{
    if (m_timeoutMs <= 0) {
        return true;
    }

    pollfd pfd {};
    pfd.fd = m_fd;
    pfd.events = event;

    while (true) {
        // poll(2) 参数依次为：pollfd 数组、数组长度、超时时间毫秒。
        // 返回 0 表示超时，>0 表示 fd 有事件，<0 表示系统调用失败。
        int rt = poll(&pfd, 1, m_timeoutMs);
        if (rt > 0) {
            if ((pfd.revents & event) != 0) {
                m_errorCode = 0;
                m_errorInfo.clear();
                return true;
            }
            if ((pfd.revents & (POLLERR | POLLHUP | POLLNVAL)) != 0) {
                m_errorCode = operation == "write" ? ERROR_TCP_SEND_FAILED : ERROR_TCP_RECV_FAILED;
                if (operation == "connect") {
                    m_errorCode = ERROR_TCP_CONNECT_FAILED;
                }
                m_errorInfo = operation + " fd event error, revents = " + std::to_string(pfd.revents);
                return false;
            }
            continue;
        }

        if (rt == 0) {
            m_errorCode = timeoutErrorCode;
            m_errorInfo = operation + " timeout after " + std::to_string(m_timeoutMs) + " ms";
            return false;
        }

        if (errno == EINTR) {
            continue;
        }

        m_errorCode = operation == "write" ? ERROR_TCP_SEND_FAILED : ERROR_TCP_RECV_FAILED;
        if (operation == "connect") {
            m_errorCode = ERROR_TCP_CONNECT_FAILED;
        }
        m_errorInfo = operation + " poll failed: " + std::string(std::strerror(errno));
        return false;
    }
}

}
