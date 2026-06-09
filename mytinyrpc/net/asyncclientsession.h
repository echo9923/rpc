#pragma once

#include "net/netaddress.h"
#include "net/socket.h"
#include "net/tcpconnection.h"
#include "net/tinypb/tinypbcodec.h"
#include "net/tinypb/tinypbdata.h"

#include <functional>
#include <memory>
#include <string>

namespace tinyrpc {

// AsyncClientSession 管理异步 RPC Channel 的长生命周期客户端连接。
//
// 职责：保存 peer addr、fd、客户端 TcpConnection 和 codec，
// 负责连接建立、请求发送、响应接收和连接关闭。
// Channel 只负责构造 request、注册 pending、投递发送任务。
//
// 会话运行在 IOThread Reactor 中：请求写入 output buffer 后由 EPOLLOUT 推进，
// 响应由 EPOLLIN 读回调解码后交给 Channel 的 pending map。
class AsyncClientSession {
 public:
    explicit AsyncClientSession(const IPAddress& peerAddr);
    ~AsyncClientSession();

    AsyncClientSession(const AsyncClientSession&) = delete;
    AsyncClientSession& operator=(const AsyncClientSession&) = delete;

    const IPAddress& getPeerAddress() const;
    Socket getFd() const;
    bool isConnected() const;

    std::string getErrorInfo() const;
    int getErrorCode() const;

    bool connect();
    void disconnect();
    void shutdownSocket();

    bool sendRequest(TinyPbStruct *request);
    bool recvResponse(const std::string& reqId, TinyPbStruct *response, int timeoutMs = -1);
    bool flushOutput();

    using ReadCallback = std::function<void(const TinyPbStruct&)>;
    using ErrorCallback = std::function<void(int, const std::string&)>;
    void setReadCallback(ReadCallback cb);
    void setErrorCallback(ErrorCallback cb);
    void startAsyncRead();


 private:
    void handleRead();
    void notifyError(int errorCode, const std::string& errorInfo);
    void registerFdEvent();
    void unregisterFdEvent();

    IPAddress m_peerAddr;
    Socket m_fd {kInvalidSocket};
    bool m_isConnected {false};
    int m_errorCode {0};
    std::string m_errorInfo;
    std::shared_ptr<TcpConnection> m_connection;
    TinyPbCodec::Ptr m_codec;
    Reactor *m_reactor {nullptr};
    FdEvent m_fdEvent;
    ReadCallback m_readCallback;
    ErrorCallback m_errorCallback;
};

}
