# 阶段 21：真正异步 RPC 网络路径

阶段 21 的目标是把“异步接口 + IOThread 中执行同步 TcpClient”的实现，
升级为由 IOThread Reactor 驱动的异步请求发送、响应读取、pending 匹配、
timeout、cancel 和真实 TcpServer 端到端验证。

## 任务一百零一：异步 Channel 网络会话对象

已完成能力：

- 新增 `AsyncClientSession`，管理异步 Channel 的长生命周期客户端连接。
- Session 保存 peer addr、fd、客户端 `TcpConnection` 和 `TinyPbCodec`。
- Session 负责连接建立、请求发送、响应读取回调和连接关闭。
- `TinyPbRpcAsyncChannel` 构造时持有 `AsyncClientSession`，不再每次异步调用创建临时 `TcpClient`。
- `stop()` 先断开 session、停止 IOThread，再失败剩余 pending。
- 测试服务器更新为单连接多请求，验证 session 连接复用。

验证命令：

```bash
./build.sh
./build/test_tinypb_rpc_async_channel
./build/test_tinypb_async_client
./scripts/check_rpc_async.sh
```

## 任务一百零二：异步发送队列和 Reactor 写事件基础设施

已完成能力：

- `AsyncClientSession` 新增 `flushOutput()`、`registerFdEvent()`、`unregisterFdEvent()` 方法。
- Session 新增 `m_reactor` 和 `m_fdEvent` 成员；`connect()` 成功后获取当前 IOThread Reactor。
- `disconnect()` 先注销 FdEvent 再关闭连接，避免悬空 epoll 条目。
- `flushOutput()` 循环 `send()`，遇到 EAGAIN 时保留 output buffer 并等待 EPOLLOUT。
- EPOLLOUT 写事件回调继续调用 `flushOutput()`，写空后只移除 EPOLLOUT，保留 EPOLLIN。

## 任务一百零三：异步读取循环和乱序响应匹配

已完成能力：

- Session socket 在 connect 成功后切换为 non-blocking。
- `startAsyncRead()` 注册 EPOLLIN 到 IOThread Reactor。
- `handleRead()` 持续读取 socket 数据，追加到客户端 `TcpConnection` input buffer。
- 客户端 `TcpConnection` 循环解码 TinyPB response，多帧粘包会逐帧写入 response map。
- Session 通过 read callback 把每个 response 交给 `TinyPbRpcAsyncChannel::handleTinyPbResponse()`。
- Channel 通过 reqId 匹配 pending，支持乱序响应；未知 reqId 响应会被忽略。

## 任务一百零四：异步 timeout / cancel 打断网络状态

已完成能力：

- 所有完成路径通过 `takePending()` 做一次性仲裁，避免 timeout、cancel、response、stop 二次回调。
- timeout 触发后删除 pending、设置 `ERROR_RPC_ASYNC_TIMEOUT` 并运行 done。
- cancel 会删除 pending、设置 `ERROR_RPC_ASYNC_CANCELED` 并运行 done；迟到响应不会再次回调。
- `stop()` 调用 `failAllPending()`，统一失败剩余 pending。
- Session 读写错误通过 error callback 通知 Channel，Channel 会失败未完成 pending。

## 任务一百零五：真实 TcpServer 异步 RPC 端到端验收

已完成能力：

- `test_tinypb_server_client` 增加 `--async-client` 模式，用真实 `TcpServer` + `TinyPbDispatcher` + Protobuf Service 验证异步客户端。
- `scripts/check_rpc_async.sh` 启动真实服务端、探测端口、运行异步客户端，再清理服务端。
- `test_tinypb_rpc_async_channel` 增加流水线发送测试：服务端在首个响应前必须能读到多个请求，防止默认路径退回阻塞 `recvResponse()`。
- `test_tinypb_async_client` 的脚本 mock server 支持单次 read 中解析多个 TinyPB 帧，适配异步流水线发送。

验证命令：

```bash
./build.sh
./build/test_tinypb_rpc_async_channel
./build/test_tinypb_async_client
MYTINYRPC_SKIP_BUILD=1 ./scripts/check_rpc_async.sh
./scripts/check_all.sh
```

## 当前默认网络路径

1. `TinyPbRpcAsyncChannel::CallMethod()` 构造 TinyPB request，注册 pending 和 timeout。
2. Channel 把发送任务投递到 IOThread。
3. IOThread 中的 `AsyncClientSession` 建立或复用 non-blocking socket。
4. `sendRequest()` 将请求追加到 output buffer，并通过 `flushOutput()` / EPOLLOUT 推进写出。
5. EPOLLIN 到来后，`handleRead()` 解码 response 并回调 Channel。
6. Channel 按 reqId 取出 pending，反序列化业务 response，运行 done。

阶段 21 不再把 `TcpClient::sendAndRecvTinyPb()` 或 `AsyncClientSession::recvResponse()` 作为异步 Channel 的默认网络路径。`recvResponse()` 仅保留为兼容/调试辅助接口。

## 当前限制

- 不实现连接池、负载均衡、重试策略或业务级取消传播协议。
- timeout 当前使用独立等待线程驱动 pending 清理，尚未统一迁入 IOThread Reactor Timer。
- 不实现发送队列优先级、背压策略或应用层流控。

阶段 21 已完成：异步 Channel 具备长生命周期 session、non-blocking socket、EPOLLIN/EPOLLOUT、pending map、乱序响应匹配、timeout/cancel/stop 生命周期和真实 TcpServer E2E 验证。
