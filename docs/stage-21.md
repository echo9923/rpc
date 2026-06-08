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
