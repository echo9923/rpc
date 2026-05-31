#pragma once

#include "net/netaddress.h"
#include "net/socket.h"
#include "net/tcpbuffer.h"
#include "net/tinypb/tinypbcodec.h"
#include "net/tinypb/tinypbdata.h"

#include <string>

namespace tinyrpc {

// TcpClient 是一个最小同步 TCP 客户端，负责：
//   1. 保存对端地址
//   2. 创建 socket 并调用 connect()
//   3. 记录连接错误
//   4. 关闭 socket
//   5. 发送/接收一个 TinyPB 请求/响应帧
// 析构时自动释放资源。
//
// 不包含：RpcChannel、超时、重试、连接池、
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

    // 返回最近一次同步网络操作的框架错误码，0 表示无错误。
    int getErrorCode() const;

    // 设置同步 connect/read/write 超时时间，单位毫秒。
    // timeoutMs <= 0 表示使用阻塞等待，不启用 poll 超时。
    void setTimeout(int timeoutMs);

    // 返回当前同步网络操作超时时间，单位毫秒。
    int getTimeout() const;

    // 设置连接失败后的有限重试次数和重试间隔。
    // retryCount 表示失败后额外重试次数，0 表示不重试。
    void setConnectRetry(int retryCount, int retryIntervalMs);

    // 创建 socket 并调用阻塞式 connect() 连接对端。
    // 成功返回 true，m_isConnected 置为 true。
    // 失败返回 false，关闭 socket，错误信息可通过 getErrorInfo() 获取。
    // 若已处于连接状态则直接返回 true。
    bool connectServer();

    // 关闭 socket 并重置连接状态。
    // 多次调用安全（幂等）。
    void closeConnection();

    // 将 TinyPbStruct 编码为 TinyPB 请求帧并完整写入 socket。
    // 若当前未连接，会先调用 connectServer() 建立阻塞式 TCP 连接。
    bool sendTinyPbRequest(TinyPbStruct *request);

    // 从 socket 读取字节流，直到解码出一个完整 TinyPB 响应帧。
    // 当前方法要求调用前已经连接，不会单独发起 connect()。
    bool recvTinyPbResponse(TinyPbStruct *response);

    // 最小同步请求/响应闭环：先发送 TinyPB 请求，再读取 TinyPB 响应。
    bool sendAndRecvTinyPb(TinyPbStruct *request, TinyPbStruct *response);

 private:
    bool connectOnce();
    bool waitFdEvent(short event, const std::string& operation, int timeoutErrorCode);
    bool writeAll(const char *data, size_t len);
    bool readSomeToBuffer(TcpBuffer *buffer);

    IPAddress m_peerAddr;
    Socket m_fd {kInvalidSocket};
    bool m_isConnected {false};
    int m_errorCode {0};
    int m_timeoutMs {0};
    int m_connectRetryCount {0};
    int m_connectRetryIntervalMs {0};
    std::string m_errorInfo;
};

}
