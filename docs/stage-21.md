# 阶段 21：真正异步 RPC 网络路径

阶段 21 的目标是把当前"异步接口 + IOThread 中执行同步 TcpClient"的实现，
升级为真正由 Reactor 驱动的异步请求发送、响应读取、pending 匹配、timeout 和 cancel。

## 任务一百零一：异步 Channel 网络会话对象

已完成能力：

- 新增 `AsyncClientSession`，管理异步 Channel 的长生命周期客户端连接。
- Session 保存 peer addr、fd、客户端 `TcpConnection` 和 `TinyPbCodec`。
- Session 负责连接建立（`connect()`）、请求发送（`sendRequest()`）、响应接收（`recvResponse()`）和连接关闭（`disconnect()`）。
- `TinyPbRpcAsyncChannel` 构造时持有 `AsyncClientSession`，替代每次创建临时 `TcpClient`。
- 默认 sync fallback 路径通过 session 在 IOThread 上执行 connect/send/recv，多次调用复用同一 session。
- `stop()` 时先断开 session 再停止 IOThread。
- 测试服务器更新为 accept 一次后在同一连接上处理多个请求，验证 session 连接复用。

验证命令：

```bash
./build.sh
./build/test_tinypb_rpc_async_channel
./build/test_tinypb_async_client
./scripts/check_rpc_async.sh
```

当前限制：

- Session 的 connect/send/recv 仍在 IOThread 上以阻塞方式执行。
- 不实现真正异步发送队列和 Reactor 写事件。
- 不实现异步读取循环。
- 不做连接池。

## 任务一百零二：异步发送队列和 Reactor 写事件基础设施

已完成能力：

- `AsyncClientSession` 新增 `flushOutput()`、`registerFdEvent()`、`unregisterFdEvent()` 方法。
- Session 新增 `m_reactor`（从 `Reactor::getCurrentReactor()` 获取）和 `m_fdEvent` 成员。
- `connect()` 成功后自动获取当前线程 Reactor。
- `disconnect()` 先注销 FdEvent 再关闭连接，防止悬空 epoll 条目。
- `flushOutput()` 循环 send 直到缓冲区清空或遇到 EAGAIN，EAGAIN 时返回 false 供调用方注册 EPOLLOUT。
- `registerFdEvent()` 向 Reactor 注册 EPOLLOUT，回调为 `flushOutput()`。
- sync fallback 路径的 `sendRequest()` 仍保持阻塞 send 循环，确保当前行为不变。

当前限制：

- `sendRequest()` 尚未切换到 `flushOutput()` + EPOLLOUT 异步模型，留到任务 103 统一升级。
- 不做异步读取循环、乱序响应匹配。

## Task 103: Async read loop infrastructure and non-blocking socket

Capabilities added:

- Session socket switched to non-blocking after connect for EPOLLIN/EPOLLOUT support.
- EPOLLIN read loop: handleRead reads socket data, decodes TinyPB frames, routes responses through readCallback.
- setReadCallback/startAsyncRead API for async response delivery.
- sendRequest and recvResponse handle EAGAIN via poll-based wait on non-blocking socket.
- Channel sets readCallback before connect; sync fallback still uses poll-based recvResponse per request.
- flushOutput and EPOLLOUT registration preserve EPOLLIN state correctly.

Current limitations:

- Sync fallback still blocks IOThread task on recvResponse; full EPOLLIN-driven delivery deferred to task 104+.
- Rapid multi-request EPOLLIN delivery has a timing issue under investigation.
