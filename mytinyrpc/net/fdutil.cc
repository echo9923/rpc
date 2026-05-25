#include "net/fdutil.h"
#include "comm/log.h"

#include <fcntl.h>
#include <sys/socket.h>

#include <string>

namespace tinyrpc {

bool setNonBlock(int fd)
{
    int flags = fcntl(fd, F_GETFL, 0);
    if (flags < 0) {
        ErrorLog(
            "fcntl F_GETFL failed, fd = " +
            std::to_string(fd)
        );
        return false;
    }

    if (fcntl(fd, F_SETFL, flags | O_NONBLOCK) < 0) {
        ErrorLog(
            "fcntl F_SETFL O_NONBLOCK failed, fd = " +
            std::to_string(fd)
        );
        return false;
    }

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
