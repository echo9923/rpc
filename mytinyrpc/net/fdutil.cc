#include "net/fdutil.h"
#include "comm/log.h"

#include <fcntl.h>
#include <sys/socket.h>

#include <string>

namespace tinyrpc {

bool setNonBlock(int fd)
{
    // 将文件描述符 fd 设置为非阻塞模式。
    // 在非阻塞模式下，当 read/write/accept 等操作无法立即完成时，
    // 系统调用会立即返回错误（errno 通常为 EAGAIN 或 EWOULDBLOCK），
    // 而不是一直阻塞等待，从而配合事件循环（如 epoll）实现高效的 I/O 多路复用。

    // 第一步：通过 fcntl 的 F_GETFL 命令获取当前 fd 的文件状态标志（file status flags）。
    // 必须先读取已有标志，避免直接覆盖丢失其他标志位（例如 O_APPEND 等）。
    int flags = fcntl(fd, F_GETFL, 0);
    if (flags < 0) {
        // 获取标志失败，记录错误日志并返回 false 表示设置未成功。
        ErrorLog(
            "fcntl F_GETFL failed, fd = " +
            std::to_string(fd)
        );
        return false;
    }

    // 第二步：通过 fcntl 的 F_SETFL 命令设置文件状态标志，
    // 在原有标志基础上追加 O_NONBLOCK，使该 fd 进入非阻塞模式。
    if (fcntl(fd, F_SETFL, flags | O_NONBLOCK) < 0) {
        // 设置非阻塞标志失败，记录错误日志并返回 false。
        ErrorLog(
            "fcntl F_SETFL O_NONBLOCK failed, fd = " +
            std::to_string(fd)
        );
        return false;
    }

    // 成功设置非阻塞模式，返回 true。
    return true;
}

bool setReuseAddr(int fd)
{
    // 允许监听 socket 复用本地地址，避免进程重启后端口长时间处于占用状态。
    int value = 1;
    if (setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &value, sizeof(value)) < 0) {
        ErrorLog(
            "setsockopt SO_REUSEADDR failed, fd = " +
            std::to_string(fd)
        );
        return false;
    }

    return true;
}

}
