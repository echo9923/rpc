#include "coroutine/coroutinehook.h"

#include "coroutine/coroutine.h"
#include "net/reactor.h"
#include "net/timer.h"

#include <cerrno>
#include <cstdint>
#include <sys/epoll.h>
#include <sys/socket.h>
#include <unistd.h>

#include <memory>

namespace tinyrpc {

namespace {

struct ConnectHookState {
    Coroutine *m_coroutine {nullptr};
    FdEvent *m_fdEvent {nullptr};
    bool m_timedOut {false};
    bool m_finished {false};
};

struct SleepHookState {
    Coroutine *m_coroutine {nullptr};
    bool m_finished {false};
};

struct FdHookWaitState {
    Coroutine *m_coroutine {nullptr};
    FdEvent *m_fdEvent {nullptr};
    bool m_timedOut {false};
    bool m_finished {false};
};

int64_t microsecondsToMilliseconds(useconds_t usec)
{
    return (static_cast<int64_t>(usec) + 999) / 1000;
}

bool isWaitableSocketError(int error)
{
    return error == EAGAIN || error == EWOULDBLOCK || error == EINTR;
}

bool waitFdEvent(FdEvent *fdEvent, uint32_t event, int timeoutMs)
{
    if (fdEvent == nullptr) {
        return false;
    }

    auto state = std::make_shared<FdHookWaitState>();
    state->m_coroutine = Coroutine::getCurrentCoroutine();
    state->m_fdEvent = fdEvent;

    Reactor *reactor = fdEvent->getReactor();
    std::shared_ptr<TimerTask> timerTask;

    fdEvent->setCoroutine(state->m_coroutine);
    fdEvent->setCoroutineListenEvent(event);
    fdEvent->addListenEvent(event);

    if (reactor != nullptr) {
        if (fdEvent->isRegistered()) {
            fdEvent->updateToReactor();
        } else {
            fdEvent->registerToReactor();
        }

        if (timeoutMs > 0 && reactor->getTimer() != nullptr) {
            // TimerTask 到期后恢复协程，并清理 FdEvent 上的协程挂载。
            // timeoutMs 单位为毫秒；仅用于本次等待，不改变 fd 自身属性。
            timerTask = std::make_shared<TimerTask>(timeoutMs, false, [state]() {
                if (state->m_finished) {
                    return;
                }
                state->m_timedOut = true;
                state->m_fdEvent->clearCoroutine();
                state->m_coroutine->resume();
            });
            reactor->getTimer()->addTimerTask(timerTask);
        }
    }

    Coroutine::yield();
    state->m_finished = true;

    if (timerTask != nullptr && reactor != nullptr && reactor->getTimer() != nullptr) {
        reactor->getTimer()->delTimerTask(timerTask);
    }

    fdEvent->delListenEvent(event);
    if (fdEvent->isRegistered()) {
        fdEvent->updateToReactor();
    }

    return state->m_timedOut;
}

bool yieldByTimer(Reactor *reactor, int64_t intervalMs)
{
    if (reactor == nullptr || reactor->getTimer() == nullptr) {
        return false;
    }

    auto state = std::make_shared<SleepHookState>();
    state->m_coroutine = Coroutine::getCurrentCoroutine();

    // TimerTask 到期回调运行在 Reactor 线程中；这里直接恢复同线程内挂起的协程。
    auto timerTask = std::make_shared<TimerTask>(intervalMs, false, [state]() {
        if (state->m_finished) {
            return;
        }
        state->m_coroutine->resume();
    });
    if (!reactor->getTimer()->addTimerTask(timerTask)) {
        return false;
    }

    Coroutine::yield();
    state->m_finished = true;
    reactor->getTimer()->delTimerTask(timerTask);
    return true;
}

}  // namespace

ssize_t readHook(FdEvent *fdEvent, void *buf, size_t count)
{
    // fdEvent->getFd()：取出此 FdEvent 管理的文件描述符。
    int fd = fdEvent->getFd();

    // ::read(fd, buf, count)：从 fd 读取最多 count 字节到 buf。
    // 非阻塞 fd 上暂无数据时返回 -1 并置 errno = EAGAIN/EWOULDBLOCK。
    ssize_t ret = ::read(fd, buf, count);

    // 主协程中不做任何挂起，直接透传系统调用结果。
    if (Coroutine::isMainCoroutine()) {
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
    fdEvent->setCoroutine(Coroutine::getCurrentCoroutine());

    // 记录协程等待的事件类型，Reactor 据此判断是否应该恢复协程。
    fdEvent->setCoroutineListenEvent(EPOLLIN);

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
    Coroutine::yield();

    // 协程恢复后再次尝试读取，返回最终结果。
    return ::read(fd, buf, count);
}

ssize_t writeHook(FdEvent *fdEvent, const void *buf, size_t count)
{
    // fdEvent->getFd()：取出此 FdEvent 管理的文件描述符。
    int fd = fdEvent->getFd();

    // ::write(fd, buf, count)：将 buf 中最多 count 字节写入 fd。
    // 非阻塞 fd 上发送缓冲区满时返回 -1 并置 errno = EAGAIN/EWOULDBLOCK。
    ssize_t ret = ::write(fd, buf, count);

    // 主协程中不做任何挂起，直接透传系统调用结果。
    if (Coroutine::isMainCoroutine()) {
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
    fdEvent->setCoroutine(Coroutine::getCurrentCoroutine());

    // 记录协程等待的事件类型，Reactor 据此判断是否应该恢复协程。
    fdEvent->setCoroutineListenEvent(EPOLLOUT);

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
    Coroutine::yield();

    // 协程恢复后再次尝试写入，返回最终结果。
    return ::write(fd, buf, count);
}

ssize_t recvHook(FdEvent *fdEvent, void *buf, size_t count, int flags, int timeoutMs)
{
    int fd = fdEvent->getFd();

    // recv(2) 参数依次为：socket fd、接收缓冲区、最多接收字节数、接收标志。
    // flags 可传 0 或 MSG_DONTWAIT/MSG_PEEK 等；当前 hook 只关心 EAGAIN 等待语义。
    ssize_t ret = ::recv(fd, buf, count, flags);

    if (Coroutine::isMainCoroutine()) {
        return ret;
    }

    if (ret >= 0) {
        return ret;
    }
    if (!isWaitableSocketError(errno)) {
        return ret;
    }

    if (waitFdEvent(fdEvent, EPOLLIN, timeoutMs)) {
        errno = ETIMEDOUT;
        return -1;
    }

    return ::recv(fd, buf, count, flags);
}

ssize_t sendHook(FdEvent *fdEvent, const void *buf, size_t count, int flags, int timeoutMs)
{
    int fd = fdEvent->getFd();

    // send(2) 参数依次为：socket fd、待发送缓冲区、最多发送字节数、发送标志。
    // 非阻塞 socket 发送缓冲区满时返回 -1，并设置 errno = EAGAIN/EWOULDBLOCK。
    ssize_t ret = ::send(fd, buf, count, flags);

    if (Coroutine::isMainCoroutine()) {
        return ret;
    }

    if (ret >= 0) {
        return ret;
    }
    if (!isWaitableSocketError(errno)) {
        return ret;
    }

    if (waitFdEvent(fdEvent, EPOLLOUT, timeoutMs)) {
        errno = ETIMEDOUT;
        return -1;
    }

    return ::send(fd, buf, count, flags);
}

int acceptHook(FdEvent *fdEvent, sockaddr *addr, socklen_t *addrLen, int timeoutMs)
{
    int fd = fdEvent->getFd();

    // accept(2) 参数依次为：监听 socket fd、输出对端地址、地址长度指针。
    // 非阻塞监听 fd 暂无连接时返回 -1，并设置 errno = EAGAIN/EWOULDBLOCK。
    int ret = ::accept(fd, addr, addrLen);

    if (Coroutine::isMainCoroutine()) {
        return ret;
    }

    if (ret >= 0) {
        return ret;
    }
    if (!isWaitableSocketError(errno)) {
        return ret;
    }

    if (waitFdEvent(fdEvent, EPOLLIN, timeoutMs)) {
        errno = ETIMEDOUT;
        return -1;
    }

    return ::accept(fd, addr, addrLen);
}

int connectHook(FdEvent *fdEvent, const sockaddr *addr, socklen_t addrLen, int timeoutMs)
{
    int fd = fdEvent->getFd();

    // connect(2) 参数依次为：socket fd、目标地址结构、地址长度。
    // 对非阻塞 fd，连接尚未完成时返回 -1 并置 errno = EINPROGRESS。
    int ret = ::connect(fd, addr, addrLen);

    if (Coroutine::isMainCoroutine()) {
        return ret;
    }

    if (ret == 0) {
        return 0;
    }
    if (errno != EINPROGRESS && errno != EALREADY && errno != EINTR) {
        return ret;
    }

    auto state = std::make_shared<ConnectHookState>();
    state->m_coroutine = Coroutine::getCurrentCoroutine();
    state->m_fdEvent = fdEvent;
    std::shared_ptr<TimerTask> timerTask;

    fdEvent->setCoroutine(state->m_coroutine);
    fdEvent->setCoroutineListenEvent(EPOLLOUT);
    fdEvent->addListenEvent(EPOLLOUT);

    Reactor *reactor = fdEvent->getReactor();
    if (reactor != nullptr) {
        if (fdEvent->isRegistered()) {
            fdEvent->updateToReactor();
        } else {
            fdEvent->registerToReactor();
        }

        if (timeoutMs > 0 && reactor->getTimer() != nullptr) {
            // TimerTask 到期后恢复同一个协程；恢复后通过 timedOut 标记返回 ETIMEDOUT。
            timerTask = std::make_shared<TimerTask>(timeoutMs, false, [state]() {
                if (state->m_finished) {
                    return;
                }
                state->m_timedOut = true;
                state->m_fdEvent->clearCoroutine();
                state->m_coroutine->resume();
            });
            reactor->getTimer()->addTimerTask(timerTask);
        }
    }

    Coroutine::yield();
    state->m_finished = true;

    if (timerTask != nullptr && reactor != nullptr && reactor->getTimer() != nullptr) {
        reactor->getTimer()->delTimerTask(timerTask);
    }

    fdEvent->delListenEvent(EPOLLOUT);
    if (fdEvent->isRegistered()) {
        fdEvent->updateToReactor();
    }

    if (state->m_timedOut) {
        errno = ETIMEDOUT;
        return -1;
    }

    int socketError = 0;
    socklen_t optLen = sizeof(socketError);
    // getsockopt(SO_ERROR) 读取非阻塞 connect 的最终结果：
    // 0 表示连接成功，非 0 表示内核保存的连接错误码。
    if (getsockopt(fd, SOL_SOCKET, SO_ERROR, &socketError, &optLen) != 0) {
        return -1;
    }
    if (socketError != 0) {
        errno = socketError;
        return -1;
    }
    return 0;
}

unsigned int sleepHook(Reactor *reactor, unsigned int seconds)
{
    // sleep(3) 参数为秒数；返回值为被信号中断时剩余的秒数。
    // 主协程中不挂起，保持与系统调用一致的阻塞语义。
    if (Coroutine::isMainCoroutine()) {
        return ::sleep(seconds);
    }

    if (seconds == 0) {
        return 0;
    }

    if (!yieldByTimer(reactor, static_cast<int64_t>(seconds) * 1000)) {
        return ::sleep(seconds);
    }
    return 0;
}

int usleepHook(Reactor *reactor, useconds_t usec)
{
    // usleep(3) 参数为微秒数；成功返回 0，失败返回 -1 并设置 errno。
    // 主协程中直接透传，避免改变非协程调用路径的行为。
    if (Coroutine::isMainCoroutine()) {
        return ::usleep(usec);
    }

    if (usec == 0) {
        return 0;
    }

    if (!yieldByTimer(reactor, microsecondsToMilliseconds(usec))) {
        return ::usleep(usec);
    }
    return 0;
}

}  // namespace tinyrpc
