#pragma once

#include "net/fdevent.h"
#include "net/reactor.h"
#include "net/socket.h"
#include "net/tcpbuffer.h"

#include <functional>
#include <memory>
#include <string>

namespace tinyrpc {

class Coroutine;

enum class ReadStatus {
    Data,
    Again,
    Closed,
    Error
};

struct ReadResult {
    ReadStatus status = ReadStatus::Error;
    std::string data;
};

class TcpConnection : public std::enable_shared_from_this<TcpConnection> {
 public:
    TcpConnection(Socket fd, Reactor *reactor);

    ~TcpConnection();

    Socket getFd() const;

    void registerToReactor();
    void handleRead();
    void handleWrite();
    void closeConnection();
    void setCloseCallback(std::function<void(int)> cb);
    void sendData(const std::string& data);
    void startReadCoroutine();

 private:
    ReadResult readData();

    void enableReadEvent();
    void disableReadEvent();
    void enableWriteEvent();
    void disableWriteEvent();
    void updateEvent();
    void closeWithCallback();

    void coroutineReadLoop();

 private:
    Socket m_fd {kInvalidSocket};             // Socket 文件描述符，标识此 TCP 连接
    Reactor *m_reactor {nullptr};             // 所属 Reactor（事件驱动），用于注册/删除事件
    FdEvent m_fdEvent;                        // 文件描述符事件对象，管理可读/可写事件的注册
    std::function<void(int)> m_closeCallback; // 连接关闭时的回调函数，参数为 fd
    TcpBuffer m_inputBuffer;                  // 输入缓冲区，暂存从 Socket 读取到的数据
    TcpBuffer m_outputBuffer;                 // 输出缓冲区，暂存待发送给对端的数据
    bool m_closeAfterWrite {false};           // 写完后是否自动关闭连接（半关闭场景）
    bool m_isClosed {false};                  // 连接是否已关闭，防止重复关闭
    std::unique_ptr<Coroutine> m_readCoroutine; // 读协程，使用 read_hook 实现协程式读取
};

}
