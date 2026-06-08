#include "net/asyncclientsession.h"

#include "comm/errorcode.h"
#include "comm/log.h"
#include "net/fdutil.h"

#include <cerrno>
#include <cstring>
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

    // 连接建立后，创建客户端 TcpConnection，用于编码请求和解析响应。
    m_connection = std::make_shared<TcpConnection>(
        m_fd,
        nullptr,
        TcpConnectionType::ClientConnection,
        m_peerAddr,
        m_codec);

    m_isConnected = true;
    m_errorCode = 0;
    m_errorInfo.clear();
    return true;
}

void AsyncClientSession::disconnect()
{
    if (m_connection != nullptr) {
        m_connection->closeConnection();
        m_connection.reset();
    } else if (m_fd != kInvalidSocket) {
        ::close(m_fd);
    }

    m_fd = kInvalidSocket;
    m_isConnected = false;
}

bool AsyncClientSession::sendRequest(TinyPbStruct *request)
{
    if (!m_isConnected || m_connection == nullptr || request == nullptr) {
        m_errorCode = ERROR_TCP_SEND_FAILED;
        m_errorInfo = "session not connected or invalid argument";
        return false;
    }

    // 清空上一次残留的输出数据，然后编码当前请求。
    m_connection->getOutputBuffer()->retrieveAll();
    m_connection->encodeClientRequest(request);
    if (!request->m_encodeSucc) {
        m_errorCode = ERROR_FAILED_SERIALIZE;
        m_errorInfo = "TinyPB request encode failed";
        disconnect();
        return false;
    }

    TcpBuffer *outBuffer = m_connection->getOutputBuffer();
    size_t written = 0;
    size_t total = outBuffer->getReadableBytes();

    while (written < total) {
        // send(2) 参数依次为：socket fd、待写缓冲区、待写字节数、标志位。
        // MSG_NOSIGNAL 防止对端关闭时产生 SIGPIPE。
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
        m_errorCode = ERROR_TCP_SEND_FAILED;
        m_errorInfo = "send failed: " + std::string(std::strerror(errno));
        disconnect();
        return false;
    }

    outBuffer->retrieveAll();
    return true;
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
        ssize_t n = ::read(m_fd, data, sizeof(data));
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
        m_errorCode = ERROR_TCP_RECV_FAILED;
        m_errorInfo = "read failed: " + std::string(std::strerror(errno));
        disconnect();
        return false;
    }
}

}
