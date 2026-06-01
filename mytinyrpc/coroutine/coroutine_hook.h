#pragma once

#include "net/fdevent.h"

#include <sys/socket.h>
#include <sys/types.h>

namespace tinyrpc {

// read_hook — 协程感知的 read 封装。
//
// 调用方负责提供与 fd 关联的 FdEvent 对象（例如 TcpConnection::m_fdEvent）。
// 在非主协程中，如果 ::read 返回 EAGAIN/EWOULDBLOCK，
// 将当前协程挂到传入的 fdEvent 上并添加 EPOLLIN，
// 然后调用 Coroutine::Yield() 让出执行权。
// 恢复后再次尝试 ::read 并返回结果。
//
// 在主协程中，直接透传 ::read 的返回值，不做任何挂起操作。
ssize_t read_hook(FdEvent *fdEvent, void *buf, size_t count);

// write_hook — 协程感知的 write 封装。
//
// 调用方负责提供与 fd 关联的 FdEvent 对象。
// 与 read_hook 对称：EAGAIN/EWOULDBLOCK 时挂当前协程，
// 添加 EPOLLOUT，Yield() 让出；恢复后再次 ::write。
//
// 在主协程中，直接透传 ::write 的返回值。
ssize_t write_hook(FdEvent *fdEvent, const void *buf, size_t count);

// connect_hook — 协程感知的 connect 封装。
//
// 主协程中直接透传 ::connect()。
// 非主协程中，如果非阻塞 connect 返回 EINPROGRESS，
// 将当前协程挂到 fdEvent 并等待 EPOLLOUT；timeoutMs > 0 时还会注册定时器。
// 恢复后通过 getsockopt(SO_ERROR) 判断连接成功、拒绝或超时。
int connect_hook(FdEvent *fdEvent, const sockaddr *addr, socklen_t addrLen, int timeoutMs);

}  // namespace tinyrpc
