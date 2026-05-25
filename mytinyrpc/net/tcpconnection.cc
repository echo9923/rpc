#include "net/tcpconnection.h"
#include "comm/log.h"

#include <cerrno>
#include <chrono>
#include <thread>
#include <unistd.h>

namespace tinyrpc {

namespace {

void sleepBriefly()
{
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
}

}

TcpConnection::TcpConnection(Socket fd)
    : m_fd(fd)
{
}

TcpConnection::~TcpConnection()
{
  closeConnection();
}

Socket TcpConnection::getFd() const
{
    return m_fd;
}

void TcpConnection::handle()
{
    InfoLog("TcpConnection handle, fd = " + std::to_string(m_fd));

    bool running = true;
    while (running) {
        ReadResult result = readData();

        switch (result.status) {
        case ReadStatus::Ok:
            InfoLog("TcpConnection receive from fd = " + std::to_string(m_fd) + ", data = " + result.data);
            if (!writeData(result.data)) {
                ErrorLog("TcpConnection write failed, fd = " + std::to_string(m_fd));
                running = false;
            }
            break;
        case ReadStatus::Again:
            sleepBriefly();
            break;
        case ReadStatus::Closed:
        case ReadStatus::Error:
            running = false;
            break;
        }
    }

    closeConnection();
}

void TcpConnection::closeConnection()
{
    if (m_fd != kInvalidSocket) {
        InfoLog("TcpConnection close, fd = " + std::to_string(m_fd));
        close(m_fd);
        m_fd = kInvalidSocket;
    }
}

ReadResult TcpConnection::readData()
{
    char buffer[1024] = {0};

    ssize_t n = read(m_fd, buffer, sizeof(buffer) - 1);

    if (n < 0) {
        if (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR) {
            return {ReadStatus::Again, {}};
        }
        ErrorLog(
            "read failed, fd = " +
            std::to_string(m_fd)
        );
        return {ReadStatus::Error, {}};
    }

    if (n == 0) {
        InfoLog("client closed, fd = " + std::to_string(m_fd));
        return {ReadStatus::Closed, {}};
    }

    return {ReadStatus::Ok, std::string(buffer, static_cast<size_t>(n))};
}

bool TcpConnection::writeData(const std::string& data)
{
    size_t totalWritten = 0;

    while (totalWritten < data.size()) {
        ssize_t n = write(
            m_fd,
            data.data() + totalWritten,
            data.size() - totalWritten
        );

        if (n < 0) {
            if (errno == EINTR) {
                continue;
            }
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                sleepBriefly();
                continue;
            }
            ErrorLog(
                "write failed, fd = " +
                std::to_string(m_fd)
            );
            return false;
        }

        if (n == 0) {
            ErrorLog("write returned 0, fd = " + std::to_string(m_fd));
            return false;
        }

        totalWritten += static_cast<size_t>(n);
    }

    InfoLog(
        "TcpConnection write to fd = " +
        std::to_string(m_fd) +
        ", bytes = " +
        std::to_string(totalWritten)
    );

    return true;
}

}
