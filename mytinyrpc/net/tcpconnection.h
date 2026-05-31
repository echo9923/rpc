#pragma once

#include "net/abstractcodec.h"
#include "net/abstractdispatcher.h"
#include "net/fdevent.h"
#include "net/reactor.h"
#include "net/socket.h"
#include "net/tcpbuffer.h"

#include <cstdint>
#include <functional>
#include <memory>
#include <string>

namespace tinyrpc {

class Coroutine;

class TcpConnection : public std::enable_shared_from_this<TcpConnection> {
 public:
    TcpConnection(Socket fd, Reactor *reactor,
                  AbstractCodec::Ptr codec = nullptr,
                  AbstractDispatcher::Ptr dispatcher = nullptr);

    ~TcpConnection();

    Socket getFd() const;

    AbstractCodec::Ptr getCodec() const;
    TcpBuffer* getInputBuffer();
    TcpBuffer* getOutputBuffer();
    void sendProtocolData(AbstractData *data);
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

 private:
    Socket m_fd {kInvalidSocket};             // Socket 文件描述符，标识此 TCP 连接
    Reactor *m_reactor {nullptr};             // 所属 Reactor（事件驱动），用于注册/删除事件
    FdEvent m_fdEvent;                        // 文件描述符事件对象，管理可读/可写事件的注册
    AbstractCodec::Ptr m_codec;               // 协议编解码器，nullptr 时走 Echo 语义
    AbstractDispatcher::Ptr m_dispatcher;     // 协议分发器，nullptr 时 execute() 走 encode 回环
    std::function<void(int)> m_closeCallback; // 连接关闭时的回调函数，参数为 fd
    TcpBuffer m_inputBuffer;                  // 输入缓冲区，暂存从 Socket 读取到的数据
    TcpBuffer m_outputBuffer;                 // 输出缓冲区，暂存待发送给对端的数据
    bool m_isClosed {false};                  // 连接是否已关闭，防止重复关闭
    int64_t m_lastActiveTimeMs {0};           // 最近一次读到业务数据的时间，供空闲超时判断
    std::unique_ptr<Coroutine> m_readCoroutine; // 连接协程，读写均通过 hook 完成
};

}
