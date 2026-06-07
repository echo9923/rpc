#include "net/tcpclient.h"
#include "comm/errorcode.h"
#include "comm/log.h"
#include "net/fdutil.h"
#include "net/timer.h"

#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <sys/epoll.h>
#include <sys/socket.h>
#include <unistd.h>

#include <chrono>
#include <memory>
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

    if (m_timeoutMs > 0 && !prepareFdEvent()) {
        m_errorCode = ERROR_TCP_CONNECT_FAILED;
        m_errorInfo = "prepare Reactor fd event failed before connect";
        close(m_fd);
        m_fd = kInvalidSocket;
        return false;
    }

    // connect(2)：向 m_peerAddr 发起 TCP 三次握手。
    // 返回值：0 成功；-1 失败，errno 标记原因。
    // 常见 errno：EINPROGRESS（非阻塞模式，连接进行中）、ECONNREFUSED（拒绝）、
    // ETIMEDOUT（超时）、ENETUNREACH（网络不可达）、EISCONN（已连接）。
    int rt = connect(m_fd, m_peerAddr.getSockAddr(), m_peerAddr.getSockLen());
    if (rt != 0) {
        // EINPROGRESS：非阻塞 connect 不会等待握手完成，而是立即返回。
    // 后续通过 Reactor/epoll 监听 EPOLLOUT 事件来确认连接建立（或失败）。
    if (m_timeoutMs > 0 && errno == EINPROGRESS) {
            // 情况一：非阻塞模式且有超时 —— 等待 POLLOUT 事件确认连接完成
            if (!waitFdEvent(EPOLLOUT, "connect", ERROR_TCP_TIMEOUT)) {
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
            // 情况二：阻塞模式或无超时 —— connect 已直接失败，记录 errno 并清理
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
    m_connection = std::make_shared<TcpConnection>(
        m_fd,
        nullptr,
        TcpConnectionType::ClientConnection,
        m_peerAddr,
        std::make_shared<TinyPbCodec>());
    m_errorCode = 0;
    m_errorInfo.clear();
    InfoLog("TcpClient connected to " + m_peerAddr.toString()
            + ", fd = " + std::to_string(m_fd));
    return true;
}

void TcpClient::closeConnection()
{
    if (m_fdEvent.isRegistered()) {
        m_fdEvent.unregisterFromReactor();
    }

    if (m_connection != nullptr) {
        m_connection->closeConnection();
        DebugLog("TcpClient closed fd = " + std::to_string(m_fd));
    } else if (m_fd != kInvalidSocket) {
        // close(2) 参数是待关闭的 socket fd；此分支只处理连接对象尚未创建的早期失败路径。
        close(m_fd);
        DebugLog("TcpClient closed raw fd = " + std::to_string(m_fd));
    }
    resetConnectionState();
}

void TcpClient::resetConnectionState()
{
    m_fd = kInvalidSocket;
    m_isConnected = false;
    m_connection.reset();
    m_fdEvent.clearCoroutine();
    m_fdEvent.setFd(kInvalidSocket);
}

Reactor* TcpClient::getOrCreateReactor()
{
    if (m_reactor != nullptr) {
        return m_reactor;
    }

    m_reactor = Reactor::getCurrentReactor();
    if (m_reactor != nullptr) {
        return m_reactor;
    }

    m_ownedReactor = std::make_unique<Reactor>();
    m_reactor = m_ownedReactor.get();
    return m_reactor;
}

bool TcpClient::prepareFdEvent()
{
    Reactor *reactor = getOrCreateReactor();
    if (reactor == nullptr || m_fd == kInvalidSocket) {
        return false;
    }

    m_fdEvent.setFd(m_fd);
    m_fdEvent.setReactor(reactor);
    return true;
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

    if (m_connection == nullptr) {
        m_errorCode = 0;
        m_errorInfo = "TcpClient connection is null";
        return false;
    }

    TcpBuffer *outBuffer = m_connection->getOutputBuffer();
    outBuffer->retrieveAll();
    m_connection->encodeClientRequest(request);
    if (!request->m_encodeSucc) {
        m_errorCode = 0;
        m_errorInfo = "TinyPB request encode failed";
        return false;
    }

    bool ok = writeAll(outBuffer->getReadPtr(), outBuffer->getReadableBytes());
    if (ok) {
        outBuffer->retrieveAll();
    }
    return ok;
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

    if (m_connection == nullptr) {
        m_errorCode = 0;
        m_errorInfo = "TcpClient connection is null";
        return false;
    }

    if (response->m_reqId.empty() && m_connection->popClientResponse(response)) {
        m_errorCode = 0;
        m_errorInfo.clear();
        return true;
    }
    if (!response->m_reqId.empty() && m_connection->getClientResponse(response->m_reqId, response)) {
        m_errorCode = 0;
        m_errorInfo.clear();
        return true;
    }

    while (true) {
        m_connection->parseClientResponses();
        if (response->m_reqId.empty() && m_connection->popClientResponse(response)) {
            m_errorCode = 0;
            m_errorInfo.clear();
            return true;
        }
        if (!response->m_reqId.empty() && m_connection->getClientResponse(response->m_reqId, response)) {
            m_errorCode = 0;
            m_errorInfo.clear();
            return true;
        }

        if (!readSomeToBuffer(m_connection->getInputBuffer())) {
            return false;
        }

        if (m_connection->getInputBuffer()->getReadableBytes() > static_cast<size_t>(kTinyPbMaxPackageLength)) {
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
        if (!waitFdEvent(EPOLLOUT, "write", ERROR_TCP_TIMEOUT)) {
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
        if (!waitFdEvent(EPOLLIN, "read", ERROR_TCP_TIMEOUT)) {
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

bool TcpClient::waitFdEvent(uint32_t event, const std::string& operation, int timeoutErrorCode)
{
    if (m_timeoutMs <= 0) {
        return true;
    }

    if (!prepareFdEvent()) {
        m_errorCode = operation == "write" ? ERROR_TCP_SEND_FAILED : ERROR_TCP_RECV_FAILED;
        if (operation == "connect") {
            m_errorCode = ERROR_TCP_CONNECT_FAILED;
        }
        m_errorInfo = operation + " prepare Reactor fd event failed";
        return false;
    }

    uint32_t epollEvent = event;
    auto result = std::make_shared<FdWaitResult>();
    auto timerTask = std::make_shared<TimerTask>(m_timeoutMs, false, [result]() {
        if (result->m_ready || result->m_failed) {
            return;
        }
        result->m_timedOut = true;
    });

    m_fdEvent.setReadCallback([result]() {
        result->m_ready = true;
        result->m_revents |= EPOLLIN;
    });
    m_fdEvent.setWriteCallback([result]() {
        result->m_ready = true;
        result->m_revents |= EPOLLOUT;
    });
    m_fdEvent.addListenEvent(epollEvent | EPOLLERR | EPOLLHUP);

    if (m_fdEvent.isRegistered()) {
        if (!m_fdEvent.updateToReactor()) {
            result->m_failed = true;
        }
    } else if (!m_fdEvent.registerToReactor()) {
        result->m_failed = true;
    }

    Reactor *reactor = getOrCreateReactor();
    if (!result->m_failed && reactor != nullptr && reactor->getTimer() != nullptr) {
        reactor->getTimer()->addTimerTask(timerTask);
    }

    while (!result->m_ready && !result->m_timedOut && !result->m_failed) {
        // epoll_wait(2) 由 Reactor::waitOnce() 封装；timeout=-1 表示等到 fd 或 timerfd 事件到来。
        int rt = reactor->waitOnce(-1);
        if (rt < 0) {
            result->m_failed = true;
        }
    }

    if (reactor != nullptr && reactor->getTimer() != nullptr) {
        reactor->getTimer()->delTimerTask(timerTask);
    }
    m_fdEvent.delListenEvent(epollEvent | EPOLLERR | EPOLLHUP);
    if (m_fdEvent.isRegistered()) {
        m_fdEvent.unregisterFromReactor();
    }

    if (result->m_ready) {
        m_errorCode = 0;
        m_errorInfo.clear();
        return true;
    }

    if (result->m_timedOut) {
        m_errorCode = timeoutErrorCode;
        m_errorInfo = operation + " timeout after " + std::to_string(m_timeoutMs) + " ms";
        return false;
    }

    m_errorCode = operation == "write" ? ERROR_TCP_SEND_FAILED : ERROR_TCP_RECV_FAILED;
    if (operation == "connect") {
        m_errorCode = ERROR_TCP_CONNECT_FAILED;
    }
    m_errorInfo = operation + " Reactor wait failed";
    return false;
}

}
