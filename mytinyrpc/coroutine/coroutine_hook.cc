#include "coroutine/coroutine_hook.h"

#include "coroutine/coroutine.h"

#include <cerrno>
#include <sys/epoll.h>
#include <unistd.h>

namespace tinyrpc {

ssize_t read_hook(FdEvent *fdEvent, void *buf, size_t count)
{
    // fdEvent->getFd()：取出此 FdEvent 管理的文件描述符。
    int fd = fdEvent->getFd();

    // ::read(fd, buf, count)：从 fd 读取最多 count 字节到 buf。
    // 非阻塞 fd 上暂无数据时返回 -1 并置 errno = EAGAIN/EWOULDBLOCK。
    ssize_t ret = ::read(fd, buf, count);

    // 主协程中不做任何挂起，直接透传系统调用结果。
    if (Coroutine::IsMainCoroutine()) {
        return ret;
    }

    // 读取成功（ret >= 0）或遇到非 EAGAIN 错误，直接返回。
    if (ret >= 0) {
        return ret;
    }
    if (errno != EAGAIN && errno != EWOULDBLOCK && errno != EINTR) {
        return ret;
    }

    // 非主协程且遇到 EAGAIN/EWOULDBLOCK：挂起当前协程，等待可读事件。

    // 将当前协程挂到 FdEvent，Reactor 恢复时可找到并 resume。
    fdEvent->setCoroutine(Coroutine::GetCurrentCoroutine());

    // 注册关注 EPOLLIN 事件。
    fdEvent->addListenEvent(EPOLLIN);

    // 若 FdEvent 已关联 Reactor，更新或注册事件到 epoll。
    if (fdEvent->getReactor() != nullptr) {
        if (fdEvent->isRegistered()) {
            fdEvent->updateToReactor();
        } else {
            fdEvent->registerToReactor();
        }
    }

    // 让出执行权，切回主协程。
    // 恢复后（由调用方手动 resume 或 Reactor 事件驱动）继续执行。
    Coroutine::Yield();

    // 协程恢复后再次尝试读取，返回最终结果。
    return ::read(fd, buf, count);
}

ssize_t write_hook(FdEvent *fdEvent, const void *buf, size_t count)
{
    // fdEvent->getFd()：取出此 FdEvent 管理的文件描述符。
    int fd = fdEvent->getFd();

    // ::write(fd, buf, count)：将 buf 中最多 count 字节写入 fd。
    // 非阻塞 fd 上发送缓冲区满时返回 -1 并置 errno = EAGAIN/EWOULDBLOCK。
    ssize_t ret = ::write(fd, buf, count);

    // 主协程中不做任何挂起，直接透传系统调用结果。
    if (Coroutine::IsMainCoroutine()) {
        return ret;
    }

    // 写入成功（ret >= 0）或遇到非 EAGAIN 错误，直接返回。
    if (ret >= 0) {
        return ret;
    }
    if (errno != EAGAIN && errno != EWOULDBLOCK && errno != EINTR) {
        return ret;
    }

    // 非主协程且遇到 EAGAIN/EWOULDBLOCK：挂起当前协程，等待可写事件。

    // 将当前协程挂到 FdEvent，Reactor 恢复时可找到并 resume。
    fdEvent->setCoroutine(Coroutine::GetCurrentCoroutine());

    // 注册关注 EPOLLOUT 事件。
    fdEvent->addListenEvent(EPOLLOUT);

    // 若 FdEvent 已关联 Reactor，更新或注册事件到 epoll。
    if (fdEvent->getReactor() != nullptr) {
        if (fdEvent->isRegistered()) {
            fdEvent->updateToReactor();
        } else {
            fdEvent->registerToReactor();
        }
    }

    // 让出执行权，切回主协程。
    Coroutine::Yield();

    // 协程恢复后再次尝试写入，返回最终结果。
    return ::write(fd, buf, count);
}

}  // namespace tinyrpc
