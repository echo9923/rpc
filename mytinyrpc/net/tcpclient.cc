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
        ErrorLog("TcpClient socket() failed, errno = " + std::to_string(m_errorCode));
        return false;
    }

    // 阻塞式 connect()：向 m_peerAddr 发起 TCP 三次握手。
    // 参数依次为：socket fd、对端地址（sockaddr*）、地址长度。
    int rt = connect(m_fd, m_peerAddr.getSockAddr(), m_peerAddr.getSockLen());
    if (rt != 0) {
        m_errorCode = errno;
        ErrorLog("TcpClient connect() to " + m_peerAddr.toString()
                 + " failed, errno = " + std::to_string(m_errorCode));
        close(m_fd);
        m_fd = kInvalidSocket;
        return false;
    }

    m_isConnected = true;
    m_errorCode = 0;
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

}
