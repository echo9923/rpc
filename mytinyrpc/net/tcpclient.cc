#include "net/tcpclient.h"
#include "comm/log.h"

#include <cerrno>
#include <cstring>
#include <sys/socket.h>
#include <unistd.h>

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

bool TcpClient::connectServer()
{
    // 已经连接则直接返回成功
    if (m_isConnected) {
        return true;
    }

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

    // 阻塞式 connect()：向 m_peerAddr 发起 TCP 三次握手。
    // 参数依次为：socket fd、对端地址（sockaddr*）、地址长度。
    int rt = connect(m_fd, m_peerAddr.getSockAddr(), m_peerAddr.getSockLen());
    if (rt != 0) {
        m_errorCode = errno;
        m_errorInfo.clear();
        ErrorLog("TcpClient connect() to " + m_peerAddr.toString()
                 + " failed, errno = " + std::to_string(m_errorCode));
        close(m_fd);
        m_fd = kInvalidSocket;
        return false;
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
        // write(2) 参数依次为：socket fd、待写缓冲区地址、待写字节数。
        ssize_t n = write(m_fd, data + written, len - written);
        if (n > 0) {
            written += static_cast<size_t>(n);
            continue;
        }

        if (n == 0) {
            m_errorCode = 0;
            m_errorInfo = "write returned zero";
            return false;
        }

        if (errno == EINTR) {
            continue;
        }

        m_errorCode = errno;
        if (errno == EAGAIN || errno == EWOULDBLOCK) {
            m_errorInfo = "socket is temporarily not writable; blocking wait is not implemented";
        } else {
            m_errorInfo.clear();
        }
        ErrorLog("TcpClient write() failed, errno = " + std::to_string(m_errorCode));
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
        // read(2) 参数依次为：socket fd、接收缓冲区地址、最大读取字节数。
        ssize_t n = read(m_fd, data, sizeof(data));
        if (n > 0) {
            buffer->append(data, static_cast<size_t>(n));
            m_errorCode = 0;
            m_errorInfo.clear();
            return true;
        }

        if (n == 0) {
            m_errorCode = 0;
            m_errorInfo = "peer closed connection";
            return false;
        }

        if (errno == EINTR) {
            continue;
        }

        m_errorCode = errno;
        if (errno == EAGAIN || errno == EWOULDBLOCK) {
            m_errorInfo = "socket is temporarily not readable; blocking wait is not implemented";
        } else {
            m_errorInfo.clear();
        }
        ErrorLog("TcpClient read() failed, errno = " + std::to_string(m_errorCode));
        return false;
    }
}

}
