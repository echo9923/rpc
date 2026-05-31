#pragma once

#include "net/netaddress.h"
#include "net/socket.h"

#include <string>

namespace tinyrpc {

// TcpClient 是一个最小同步 TCP 客户端，只负责：
//   1. 保存对端地址
//   2. 创建 socket 并调用 connect()
//   3. 记录连接错误
//   4. 关闭 socket
// 析构时自动释放资源。
//
// 不包含：TinyPB 编解码、RpcChannel、超时、重试、连接池、
// 异步回调、协程 hook 或 Reactor 集成。
class TcpClient {
 public:
    // 保存对端地址，此时不创建 socket，m_fd 保持 kInvalidSocket。
    explicit TcpClient(const IPAddress& peerAddr);

    // 析构时自动关闭连接，释放 socket 资源。
    ~TcpClient();

    TcpClient(const TcpClient&) = delete;
    TcpClient& operator=(const TcpClient&) = delete;

    const IPAddress& getPeerAddress() const;
    Socket getFd() const;
    bool isConnected() const;

    // 返回最近一次 connectServer() 产生的错误描述；
    // 无错误时返回空字符串。
    std::string getErrorInfo() const;

    // 创建 socket 并调用阻塞式 connect() 连接对端。
    // 成功返回 true，m_isConnected 置为 true。
    // 失败返回 false，关闭 socket，错误信息可通过 getErrorInfo() 获取。
    // 若已处于连接状态则直接返回 true。
    bool connectServer();

    // 关闭 socket 并重置连接状态。
    // 多次调用安全（幂等）。
    void closeConnection();

 private:
    IPAddress m_peerAddr;
    Socket m_fd {kInvalidSocket};
    bool m_isConnected {false};
    int m_errorCode {0};
};

}
