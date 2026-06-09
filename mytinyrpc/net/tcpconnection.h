#pragma once

#include "net/abstractcodec.h"
#include "net/abstractdispatcher.h"
#include "net/fdevent.h"
#include "net/netaddress.h"
#include "net/reactor.h"
#include "net/socket.h"
#include "net/tcpbuffer.h"
#include "net/tinypb/tinypbdata.h"

#include <cstdint>
#include <functional>
#include <unordered_map>
#include <memory>
#include <string>

namespace tinyrpc {

class Coroutine;

enum class TcpConnectionType {
    ServerConnection,
    ClientConnection
};

class TcpConnection : public std::enable_shared_from_this<TcpConnection> {
 public:
    TcpConnection(Socket fd, Reactor *reactor,
                  AbstractCodec::Ptr codec = nullptr,
                  AbstractDispatcher::Ptr dispatcher = nullptr);
    TcpConnection(Socket fd, Reactor *reactor, TcpConnectionType connectionType,
                  const IPAddress& peerAddr,
                  AbstractCodec::Ptr codec = nullptr,
                  AbstractDispatcher::Ptr dispatcher = nullptr);

    ~TcpConnection();

    Socket getFd() const;
    TcpConnectionType getConnectionType() const;
    const IPAddress& getPeerAddress() const;

    AbstractCodec::Ptr getCodec() const;
    TcpBuffer* getInputBuffer();
    TcpBuffer* getOutputBuffer();
    void sendProtocolData(AbstractData *data);
    void encodeClientRequest(TinyPbStruct *request);
    void appendClientInput(const char *data, size_t len);
    void parseClientResponses();
    bool getClientResponse(const std::string& reqId, TinyPbStruct *response);
    bool popClientResponse(TinyPbStruct *response);
    size_t getClientResponseCount() const;
    void execute();

    void closeConnection();
    void setCloseCallback(std::function<void(int)> cb);
    void sendData(const std::string& data);
    void startConnection();
    bool isClosed() const;
    int64_t getLastActiveTimeMs() const;
    void refreshActiveTime();

 private:
    friend class TcpConnectionTimeWheel;

    void closeWithCallback();
    void coroutineReadLoop();
    bool input();
    void output();
    bool shouldCloseAfterOutput() const;
    std::unique_ptr<AbstractData> createProtocolData() const;

 private:
    Socket m_fd {kInvalidSocket};             // Socket 文件描述符，标识此 TCP 连接
    Reactor *m_reactor {nullptr};             // 所属 Reactor（事件驱动），用于注册/删除事件
    FdEvent m_fdEvent;                        // 文件描述符事件对象，管理可读/可写事件的注册
    TcpConnectionType m_connectionType {TcpConnectionType::ServerConnection}; // 区分服务端连接和客户端连接语义
    IPAddress m_peerAddr {"0.0.0.0", 0};      // 对端地址；客户端连接用于记录目标服务端
    AbstractCodec::Ptr m_codec;               // 协议编解码器，nullptr 时走 Echo 语义
    AbstractDispatcher::Ptr m_dispatcher;     // 协议分发器，nullptr 时 execute() 走 encode 回环
    std::function<void(int)> m_closeCallback; // 连接关闭时的回调函数，参数为 fd
    TcpBuffer m_inputBuffer;                  // 输入缓冲区，暂存从 Socket 读取到的数据
    TcpBuffer m_outputBuffer;                 // 输出缓冲区，暂存待发送给对端的数据
    bool m_isClosed {false};                  // 连接是否已关闭，防止重复关闭
    int64_t m_lastActiveTimeMs {0};           // 最近一次读到业务数据的时间，供空闲超时判断
    std::unique_ptr<Coroutine> m_readCoroutine; // 连接协程，读写均通过 hook 完成
    std::unordered_map<std::string, TinyPbStruct> m_clientResponses; // 客户端侧按 reqId 缓存已解码响应
};

}
