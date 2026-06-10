#include "coroutine/coroutinehook.h"

#include "coroutine/coroutine.h"
#include "net/fdeventcontainer.h"
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

    // ─────────────────────────────────────────────────────────────────────────────
    // 函数指针类型别名 (using 声明)
    // 这里定义的每个 using 都是一个"函数指针类型"，指明某个系统调用函数的签名（参数列表和返回值）。
    // 例如 sys_read_t 表示一个函数指针，指向的函数签名为：
    //   ssize_t (int, void*, size_t)
    // 即：接受一个 int 文件描述符、一个 void* 缓冲区指针、一个 size_t 长度，返回 ssize_t。
    //
    // 这些类型别名会被用来声明全局变量（g_sys_read 等），
    // 这些变量通过 dlsym(RTLD_NEXT, "函数名") 动态获取 libc 中真实系统调用函数的地址，
    // 以便在 hook 函数中调用"真正的"系统调用，避免无限递归。
    // ─────────────────────────────────────────────────────────────────────────────

    using sys_read_t    = ssize_t(*)(int, void*, size_t);          // read(fd, buf, count)
    using sys_write_t   = ssize_t(*)(int, const void*, size_t);     // write(fd, buf, count)
    using sys_recv_t    = ssize_t(*)(int, void*, size_t, int);      // recv(sockfd, buf, len, flags)
    using sys_send_t    = ssize_t(*)(int, const void*, size_t, int); // send(sockfd, buf, len, flags)
    using sys_accept_t  = int(*)(int, sockaddr*, socklen_t*);       // accept(sockfd, addr, addrlen)
    using sys_connect_t = int(*)(int, const sockaddr*, socklen_t);   // connect(sockfd, addr, addrlen)
    using sys_sleep_t   = unsigned int(*)(unsigned int);            // sleep(seconds)
    using sys_usleep_t  = int(*)(useconds_t);                       // usleep(microseconds)

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

    // ConnectHookState：用于 connectHook 函数中管理非阻塞 connect 的异步等待状态。
    struct ConnectHookState {
        Coroutine *m_coroutine {nullptr};   // 发起 connect 操作的协程指针，用于超时或事件就绪时恢复执行
        FdEvent *m_fdEvent {nullptr};       // 关联的文件描述符事件对象，用于监听 EPOLLOUT 事件（连接完成）
        bool m_timedOut {false};            // 是否超时，超时时由 TimerTask 回调设为 true，返回 ETIMEDOUT
        bool m_finished {false};            // 是否已完成（正常完成或被超时回调处理后设为 true），防止重复操作
    };

    // SleepHookState：用于 sleepHook / usleepHook 函数中管理协程睡眠等待状态。
    struct SleepHookState {
        Coroutine *m_coroutine {nullptr};   // 发起睡眠的协程指针，定时器到期时恢复该协程
        bool m_finished {false};            // 是否已完成（睡眠到期或被取消），防止 TimerTask 回调重复执行
    };

    // FdHookWaitState：用于 waitFdEvent 函数中管理文件描述符 IO 事件的异步等待状态。
    // 在 recvHook / sendHook / acceptHook 等函数中通用，等待 fd 可读/可写事件。
    struct FdHookWaitState {
        Coroutine *m_coroutine {nullptr};   // 挂起等待 IO 事件的协程指针，事件就绪或超时时恢复执行
        FdEvent *m_fdEvent {nullptr};       // 关联的文件描述符事件对象，用于监听 EPOLLIN/EPOLLOUT 等事件
        bool m_timedOut {false};            // 是否超时，超时时由 TimerTask 回调设为 true，调用方据此返回 ETIMEDOUT
        bool m_finished {false};            // 是否已完成（事件就绪或超时处理完毕），防止回调重复执行
    };

int64_t microsecondsToMilliseconds(useconds_t usec)
{
    return (static_cast<int64_t>(usec) + 999) / 1000;
}

bool isWaitableSocketError(int error)
{
    return error == EAGAIN || error == EWOULDBLOCK || error == EINTR;
}

/**
 * @brief 等待文件描述符上的特定事件（可读/可写等），支持超时。
 *
 * 这是 recvHook / sendHook / acceptHook 等函数的底层阻塞等待机制。
 * 核心思路：
 *   1. 将当前协程挂载到 FdEvent 上，注册感兴趣的事件（如 EPOLLIN）。
 *   2. 如果提供超时，则启动一个 TimerTask，到期后恢复协程并标记超时。
 *   3. 挂起当前协程（yield），让出 CPU 给 Reactor 线程处理 IO 事件。
 *   4. 协程被恢复后（事件就绪 或 超时），清理定时器、取消事件监听，返回是否超时。
 *
 * @param fdEvent   要等待的文件描述符事件对象（内部持有 fd、Reactor 等）
 * @param event     等待的事件类型（如 EPOLLIN、EPOLLOUT）
 * @param timeoutMs 超时时间（毫秒）。<=0 表示不设置超时，无限等待
 * @return true     等待超时（调用方应设 errno = ETIMEDOUT 并返回 -1）
 * @return false    事件正常就绪（调用方可继续执行系统调用）
 */
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

/**
 * @brief readHook - 协程化的 read 系统调用（显式 hook 版本）。
 *
 * 协程环境下，当 read 因无数据可读返回 EAGAIN/EWOULDBLOCK/EINTR 时，
 * 将当前协程挂起，并在 fd 可读事件就绪后恢复协程再次 read，避免阻塞线程。
 *
 * 实现步骤：
 *   1. 先尝试一次真实的 read 调用（g_sys_read）。
 *   2. 若返回主协程中，直接返回结果（主协程不做 hook 挂起）。
 *   3. 若 read 成功（ret >= 0），直接返回。
 *   4. 若错误码不是 EAGAIN/EWOULDBLOCK/EINTR（即不可等待的错误），直接返回 -1。
 *   5. 否则，将当前协程挂载到 fdEvent 上，监听 EPOLLIN 事件，
 *      注册/更新 fdEvent 到 Reactor，然后 yield 挂起协程。
 *   6. 协程被 Reactor 唤醒（事件就绪）后，再次调用 g_sys_read 读取数据。
 *
 * @param fdEvent 文件描述符事件对象（内部持有 fd、Reactor 引用）
 * @param buf     数据缓冲区
 * @param count   期望读取的字节数
 * @return ssize_t 成功返回读取的字节数，失败返回 -1 并设置 errno
 */
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
//       read/write/accept/connect → 通过 FdEventContainer 取得 fd 事件并按 Reactor 事件挂起恢复
// ─────────────────────────────────────────────────────────────────────────────

extern "C" {

ssize_t read(int fd, void *buf, size_t count)
{
    using namespace tinyrpc;
    if (!IsHookEnabled() || Coroutine::isMainCoroutine()) {
        return g_sys_read(fd, buf, count);
    }
    FdEvent *fdEvent = FdEventContainer::getInstance().getOrCreate(fd);
    return readHook(fdEvent, buf, count);
}

ssize_t write(int fd, const void *buf, size_t count)
{
    using namespace tinyrpc;
    if (!IsHookEnabled() || Coroutine::isMainCoroutine()) {
        return g_sys_write(fd, buf, count);
    }
    FdEvent *fdEvent = FdEventContainer::getInstance().getOrCreate(fd);
    return writeHook(fdEvent, buf, count);
}

int accept(int fd, sockaddr *addr, socklen_t *addrLen)
{
    using namespace tinyrpc;
    if (!IsHookEnabled() || Coroutine::isMainCoroutine()) {
        return g_sys_accept(fd, addr, addrLen);
    }
    FdEvent *fdEvent = FdEventContainer::getInstance().getOrCreate(fd);
    return acceptHook(fdEvent, addr, addrLen);
}

int connect(int fd, const sockaddr *addr, socklen_t addrLen)
{
    using namespace tinyrpc;
    if (!IsHookEnabled() || Coroutine::isMainCoroutine()) {
        return g_sys_connect(fd, addr, addrLen);
    }
    FdEvent *fdEvent = FdEventContainer::getInstance().getOrCreate(fd);
    return connectHook(fdEvent, addr, addrLen, -1);
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
