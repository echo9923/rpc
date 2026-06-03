#pragma once

#include "net/fdevent.h"

#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

namespace tinyrpc {

class Reactor;

// readHook — 协程感知的 read 封装。
//
// 调用方负责提供与 fd 关联的 FdEvent 对象（例如 TcpConnection::m_fdEvent）。
// 在非主协程中，如果 ::read 返回 EAGAIN/EWOULDBLOCK，
// 将当前协程挂到传入的 fdEvent 上并添加 EPOLLIN，
// 然后调用 Coroutine::yield() 让出执行权。
// 恢复后再次尝试 ::read 并返回结果。
//
// 在主协程中，直接透传 ::read 的返回值，不做任何挂起操作。
ssize_t readHook(FdEvent *fdEvent, void *buf, size_t count);

// writeHook — 协程感知的 write 封装。
//
// 调用方负责提供与 fd 关联的 FdEvent 对象。
// 与 readHook 对称：EAGAIN/EWOULDBLOCK 时挂当前协程，
// 添加 EPOLLOUT，yield() 让出；恢复后再次 ::write。
//
// 在主协程中，直接透传 ::write 的返回值。
ssize_t writeHook(FdEvent *fdEvent, const void *buf, size_t count);

// recvHook — 协程感知的 recv 封装。
//
// 与 readHook 类似，但保留 recv(2) 的 flags 参数。
// timeoutMs > 0 时，等待 EPOLLIN 的同时注册一次性 TimerEvent；
// 超时恢复后返回 -1 并设置 errno = ETIMEDOUT。
ssize_t recvHook(FdEvent *fdEvent, void *buf, size_t count, int flags, int timeoutMs = -1);

// sendHook — 协程感知的 send 封装。
//
// 与 writeHook 类似，但保留 send(2) 的 flags 参数。
// timeoutMs > 0 时，等待 EPOLLOUT 的同时注册一次性 TimerEvent；
// 超时恢复后返回 -1 并设置 errno = ETIMEDOUT。
ssize_t sendHook(FdEvent *fdEvent, const void *buf, size_t count, int flags, int timeoutMs = -1);

// acceptHook — 协程感知的 accept 封装。
//
// 主协程中直接透传 ::accept()。非主协程中，如果非阻塞 listen fd
// 暂无连接并返回 EAGAIN/EWOULDBLOCK，则等待 EPOLLIN 后恢复。
// timeoutMs > 0 时支持超时恢复。
int acceptHook(FdEvent *fdEvent, sockaddr *addr, socklen_t *addrLen, int timeoutMs = -1);

// connectHook — 协程感知的 connect 封装。
//
// 主协程中直接透传 ::connect()。
// 非主协程中，如果非阻塞 connect 返回 EINPROGRESS，
// 将当前协程挂到 fdEvent 并等待 EPOLLOUT；timeoutMs > 0 时还会注册定时器。
// 恢复后通过 getsockopt(SO_ERROR) 判断连接成功、拒绝或超时。
int connectHook(FdEvent *fdEvent, const sockaddr *addr, socklen_t addrLen, int timeoutMs);

// sleepHook — 协程感知的 sleep 封装。
//
// 主协程中直接透传 ::sleep()。
// 非主协程中，使用传入 Reactor 的 TimerEvent 定时恢复当前协程。
// 当前实现不处理信号中断，Timer 到期恢复后固定返回 0。
unsigned int sleepHook(Reactor *reactor, unsigned int seconds);

// usleepHook — 协程感知的 usleep 封装。
//
// 主协程中直接透传 ::usleep()。
// 非主协程中，将微秒向上折算为毫秒 TimerEvent，到期后恢复当前协程。
int usleepHook(Reactor *reactor, useconds_t usec);

}  // namespace tinyrpc
