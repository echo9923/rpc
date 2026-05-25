#pragma once

#include "net/socket.h"

#include <string>

namespace tinyrpc {

enum class ReadStatus {
    Ok,
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
    explicit TcpConnection(Socket fd);

    ~TcpConnection();

    Socket getFd() const;

    void handle();

    void closeConnection();

 private:
    ReadResult readData();

    bool writeData(const std::string& data);

 private:
    Socket m_fd {kInvalidSocket};
};

}
