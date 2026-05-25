#pragma once

#include "net/fdevent.h"
#include "net/reactor.h"
#include "net/socket.h"

#include <functional>
#include <string>

namespace tinyrpc {

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

class TcpConnection {
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

 private:
    ReadResult readData();

    void enableWriteEvent();
    void disableWriteEvent();

 private:
    Socket m_fd {kInvalidSocket};
    Reactor *m_reactor {nullptr};
    FdEvent m_fdEvent;
    std::function<void(int)> m_closeCallback;
    std::string m_outputBuffer;
    bool m_isClosed {false};
};

}
