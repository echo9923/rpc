#include "coroutine/coroutinehook.h"

#include "coroutine/coroutine.h"
#include "net/reactor.h"
#include "net/timer.h"

#include <cerrno>
#include <cstdint>
#include <dlfcn.h>
#include <sys/epoll.h>
#include <sys/socket.h>
#include <unistd.h>

#include <memory>

// ─────────────────────────────────────────────────────────────────────────────
// 真实系统调用函数指针（通过 dlsym(RTLD_NEXT, ...) 获取）
//
// 所有显式 hook 函数和透明 hook 都使用这组指针调用系统调用，
// 避免透明 hook 与显式 hook 相互调用时出现无限递归。
// ─────────────────────────────────────────────────────────────────────────────

namespace {

using sys_read_t    = ssize_t(*)(int, void*, size_t);
using sys_write_t   = ssize_t(*)(int, const void*, size_t);
using sys_recv_t    = ssize_t(*)(int, void*, size_t, int);
using sys_send_t    = ssize_t(*)(int, const void*, size_t, int);
using sys_accept_t  = int(*)(int, sockaddr*, socklen_t*);
using sys_connect_t = int(*)(int, const sockaddr*, socklen_t);
using sys_sleep_t   = unsigned int(*)(unsigned int);
using sys_usleep_t  = int(*)(useconds_t);

sys_read_t    g_sys_read    = nullptr;
sys_write_t   g_sys_write   = nullptr;
sys_recv_t    g_sys_recv    = nullptr;
sys_send_t    g_sys_send    = nullptr;
sys_accept_t  g_sys_accept  = nullptr;
sys_connect_t g_sys_connect = nullptr;
sys_sleep_t   g_sys_sleep   = nullptr;
sys_usleep_t  g_sys_usleep  = nullptr;

}  // namespace

__attribute__((constructor)) static void initSysCallPointers()
{
    g_sys_read    = reinterpret_cast<sys_read_t>   (dlsym(RTLD_NEXT, "read"));
    g_sys_write   = reinterpret_cast<sys_write_t>  (dlsym(RTLD_NEXT, "write"));
    g_sys_recv    = reinterpret_cast<sys_recv_t>   (dlsym(RTLD_NEXT, "recv"));
    g_sys_send    = reinterpret_cast<sys_send_t>   (dlsym(RTLD_NEXT, "send"));
    g_sys_accept  = reinterpret_cast<sys_accept_t> (dlsym(RTLD_NEXT, "accept"));
    g_sys_connect = reinterpret_cast<sys_connect_t>(dlsym(RTLD_NEXT, "connect"));
    g_sys_sleep   = reinterpret_cast<sys_sleep_t>  (dlsym(RTLD_NEXT, "sleep"));
    g_sys_usleep  = reinterpret_cast<sys_usleep_t> (dlsym(RTLD_NEXT, "usleep"));
}

namespace tinyrpc {

// ─────────────────────────────────────────────────────────────────────────────
// 全局（线程局部）hook 开关
// ─────────────────────────────────────────────────────────────────────────────

static thread_local bool s_hookEnabled = false;

void SetHook(bool enabled)  { s_hookEnabled = enabled; }
bool IsHookEnabled()        { return s_hookEnabled; }

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
    int fd = fdEvent->getFd();
    ssize_t ret = g_sys_read(fd, buf, count);

    if (Coroutine::isMainCoroutine()) {
        return ret;
    }

    if (ret >= 0) {
        return ret;
    }
    if (errno != EAGAIN && errno != EWOULDBLOCK && errno != EINTR) {
        return ret;
    }

    fdEvent->setCoroutine(Coroutine::getCurrentCoroutine());
    fdEvent->setCoroutineListenEvent(EPOLLIN);
    fdEvent->addListenEvent(EPOLLIN);

    if (fdEvent->getReactor() != nullptr) {
        if (fdEvent->isRegistered()) {
            fdEvent->updateToReactor();
        } else {
            fdEvent->registerToReactor();
        }
    }

    Coroutine::yield();

    return g_sys_read(fd, buf, count);
}

ssize_t writeHook(FdEvent *fdEvent, const void *buf, size_t count)
{
    int fd = fdEvent->getFd();
    ssize_t ret = g_sys_write(fd, buf, count);

    if (Coroutine::isMainCoroutine()) {
        return ret;
    }

    if (ret >= 0) {
        return ret;
    }
    if (errno != EAGAIN && errno != EWOULDBLOCK && errno != EINTR) {
        return ret;
    }

    fdEvent->setCoroutine(Coroutine::getCurrentCoroutine());
    fdEvent->setCoroutineListenEvent(EPOLLOUT);
    fdEvent->addListenEvent(EPOLLOUT);

    if (fdEvent->getReactor() != nullptr) {
        if (fdEvent->isRegistered()) {
            fdEvent->updateToReactor();
        } else {
            fdEvent->registerToReactor();
        }
    }

    Coroutine::yield();

    return g_sys_write(fd, buf, count);
}

ssize_t recvHook(FdEvent *fdEvent, void *buf, size_t count, int flags, int timeoutMs)
{
    int fd = fdEvent->getFd();
    ssize_t ret = g_sys_recv(fd, buf, count, flags);

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

    return g_sys_recv(fd, buf, count, flags);
}

ssize_t sendHook(FdEvent *fdEvent, const void *buf, size_t count, int flags, int timeoutMs)
{
    int fd = fdEvent->getFd();
    ssize_t ret = g_sys_send(fd, buf, count, flags);

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

    return g_sys_send(fd, buf, count, flags);
}

int acceptHook(FdEvent *fdEvent, sockaddr *addr, socklen_t *addrLen, int timeoutMs)
{
    int fd = fdEvent->getFd();
    int ret = g_sys_accept(fd, addr, addrLen);

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

    return g_sys_accept(fd, addr, addrLen);
}

int connectHook(FdEvent *fdEvent, const sockaddr *addr, socklen_t addrLen, int timeoutMs)
{
    int fd = fdEvent->getFd();
    int ret = g_sys_connect(fd, addr, addrLen);

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
    if (Coroutine::isMainCoroutine()) {
        return g_sys_sleep(seconds);
    }

    if (seconds == 0) {
        return 0;
    }

    if (!yieldByTimer(reactor, static_cast<int64_t>(seconds) * 1000)) {
        return g_sys_sleep(seconds);
    }
    return 0;
}

int usleepHook(Reactor *reactor, useconds_t usec)
{
    if (Coroutine::isMainCoroutine()) {
        return g_sys_usleep(usec);
    }

    if (usec == 0) {
        return 0;
    }

    if (!yieldByTimer(reactor, microsecondsToMilliseconds(usec))) {
        return g_sys_usleep(usec);
    }
    return 0;
}

}  // namespace tinyrpc

// ─────────────────────────────────────────────────────────────────────────────
// 透明 hook：覆盖 libc 同名函数，对调用方无侵入。
//
// 规则：
//   - hook 关闭（s_hookEnabled == false）或主协程 → 直通 g_sys_* 真实调用
//   - hook 开启且非主协程：
//       sleep/usleep → 委托 sleepHook / usleepHook（已由 Reactor::getCurrentReactor 获取 Reactor）
//       read/write/accept/connect → TODO(task-92)：接入 FdEventContainer 后补全协程挂起路径
// ─────────────────────────────────────────────────────────────────────────────

extern "C" {

ssize_t read(int fd, void *buf, size_t count)
{
    using namespace tinyrpc;
    if (!IsHookEnabled() || Coroutine::isMainCoroutine()) {
        return g_sys_read(fd, buf, count);
    }
    // TODO(task-92): FdEventContainer::getOrCreate(fd) → readHook(fdEvent, buf, count)
    return g_sys_read(fd, buf, count);
}

ssize_t write(int fd, const void *buf, size_t count)
{
    using namespace tinyrpc;
    if (!IsHookEnabled() || Coroutine::isMainCoroutine()) {
        return g_sys_write(fd, buf, count);
    }
    // TODO(task-92): FdEventContainer::getOrCreate(fd) → writeHook(fdEvent, buf, count)
    return g_sys_write(fd, buf, count);
}

int accept(int fd, sockaddr *addr, socklen_t *addrLen)
{
    using namespace tinyrpc;
    if (!IsHookEnabled() || Coroutine::isMainCoroutine()) {
        return g_sys_accept(fd, addr, addrLen);
    }
    // TODO(task-92): FdEventContainer::getOrCreate(fd) → acceptHook(fdEvent, addr, addrLen)
    return g_sys_accept(fd, addr, addrLen);
}

int connect(int fd, const sockaddr *addr, socklen_t addrLen)
{
    using namespace tinyrpc;
    if (!IsHookEnabled() || Coroutine::isMainCoroutine()) {
        return g_sys_connect(fd, addr, addrLen);
    }
    // TODO(task-92): FdEventContainer::getOrCreate(fd) → connectHook(fdEvent, addr, addrLen, -1)
    return g_sys_connect(fd, addr, addrLen);
}

unsigned int sleep(unsigned int seconds)
{
    using namespace tinyrpc;
    if (!IsHookEnabled() || Coroutine::isMainCoroutine()) {
        return g_sys_sleep(seconds);
    }
    Reactor *reactor = Reactor::getCurrentReactor();
    return sleepHook(reactor, seconds);
}

int usleep(useconds_t usec)
{
    using namespace tinyrpc;
    if (!IsHookEnabled() || Coroutine::isMainCoroutine()) {
        return g_sys_usleep(usec);
    }
    Reactor *reactor = Reactor::getCurrentReactor();
    return usleepHook(reactor, usec);
}

}  // extern "C"
