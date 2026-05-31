# TinyRPC 复刻进度

本文记录每个任务完成后的能力增量、验证方式和当前限制，便于后续阶段回归。

## 阶段 8：同步 RPC 客户端闭环

### 任务三十八：实现最小 `TinyPbRpcChannel`

已完成能力：

- 新增 `TinyPbRpcChannel`，继承 `google::protobuf::RpcChannel`。
- Protobuf 生成的 `QueryService_Stub` 可以通过 Channel 调用 `CallMethod()`。
- Channel 会把 `MethodDescriptor::full_name()` 写入 TinyPB `serviceFullName`。
- Channel 会把 Protobuf request 序列化到 TinyPB `pbData`，并把 response `pbData` 反序列化回业务 response。
- `TinyPbRpcController` 支持记录错误码、错误文本和本次请求号。
- 新增 `test_tinypb_rpc_channel`，覆盖正常响应、服务端 TinyPB 错误、非法 response payload 和 done closure。

验证命令：

```bash
./build.sh
./build/test_tinypb_rpc_channel
./build/test_tinypb_codec
./build/test_tinypb_dispatcher
./build/test_tcp_client
```

当前限制：

- 真实 Stub 到真实 `TcpServer` 的端到端验收留到任务三十九。
- `msgReq` 自动生成工具和 mismatch 检查留到任务四十/四十六。
- 超时、重试和连接池留到后续阶段。

### 任务三十九：真实 Stub 到服务端端到端同步 RPC

已完成能力：

- 新增 `test_tinypb_server_client`，支持 `--server`、`--client`、`--probe` 三种模式。
- 服务端模式启动真实 `TcpServer`，接入 `TinyPbCodec` 和 `TinyPbDispatcher`，注册 `QueryServiceImpl`。
- 客户端模式使用 Protobuf 生成的 `QueryService_Stub` 与 `TinyPbRpcChannel` 发起真实网络 RPC。
- 新增 `scripts/check_stage8_rpc.sh`，自动启动服务端、等待端口可连接、运行 Stub 客户端并清理服务端进程。

验证命令：

```bash
./build.sh
./scripts/check_stage8_rpc.sh
./scripts/check_stage1.sh
```

当前限制：

- 只验证单客户端单请求。
- 不做超时、重试和异步 Stub。

### 任务四十：请求号与 `TinyPbRpcController` 语义补齐

已完成能力：

- 新增 `MsgReqUtil::genMsgNumber()`，生成非空、递增且进程内不重复的请求号。
- `TinyPbRpcController` 支持 `MsgReq()`、`ErrorCode()`、`ErrorText()` 和 `Timeout()` 占位。
- `TinyPbRpcChannel` 在 controller 未预设 `msgReq` 时自动生成请求号。
- `TinyPbRpcChannel` 在 controller 已预设 `msgReq` 时复用该请求号。
- Channel 收到 response 后检查 `msgReq`，不匹配时设置 `ERROR_RPC_MSGREQ_MISMATCH`。
- 新增 `test_msg_req`，并扩展 `test_tinypb_rpc_channel` 覆盖预设请求号和 mismatch。

验证命令：

```bash
./build.sh
./build/test_msg_req
./build/test_tinypb_rpc_channel
./scripts/check_stage8_rpc.sh
```

当前限制：

- `Timeout()` 仅保存数值，不驱动实际读写超时。
- 同步客户端仍不缓存乱序响应。

### 任务四十一：同步客户端超时与失败路径

已完成能力：

- `TcpClient` 新增 `setTimeout()`、`getTimeout()` 和 `getErrorCode()`。
- connect 支持非阻塞 `connect()` + `poll(POLLOUT)` 等待，失败映射为 `ERROR_TCP_CONNECT_FAILED`。
- read/write 支持 `poll(POLLIN/POLLOUT)` 等待，超时映射为 `ERROR_TCP_TIMEOUT`。
- 对端提前关闭映射为 `ERROR_TCP_RECV_FAILED`。
- Channel 会把 controller timeout 传给内部 TcpClient，并透传明确的 TcpClient 错误码。
- `test_tcp_client` 覆盖读超时、服务端提前关闭、慢响应未超时成功。
- `test_tinypb_rpc_channel` 覆盖 controller timeout 传递。

验证命令：

```bash
./build.sh
./build/test_tcp_client
./build/test_tinypb_rpc_channel
./scripts/check_stage8_rpc.sh
```

当前限制：

- 不做异步重试。
- 不做连接池。
- 不做客户端 Reactor 化。

### 任务四十二：阶段八调用链文档和同步 RPC 回归脚本

已完成能力：

- `docs/stage-8.md` 补充同步 RPC 主调用链图。
- `docs/stage-8.md` 明确阶段八暂不支持异步、连接池、多路复用和乱序响应缓存。
- 新增 `scripts/check_rpc_sync_basic.sh`，串联构建、TinyPB codec、dispatcher、TcpClient、TinyPbRpcChannel 和真实端到端同步 RPC 验收。
- README 增加阶段八同步 RPC 基础回归入口。

验证命令：

```bash
./scripts/check_rpc_sync_basic.sh
```

## 阶段 9：同步客户端连接语义收口

### 任务四十三：`TcpClient` 重连和关闭边界

已完成能力：

- `TcpClient` 新增 `setConnectRetry(retryCount, retryIntervalMs)`。
- 连接失败后关闭失败 fd，有限重试时每次重新创建 fd。
- 主动 `closeConnection()` 后，下一次 `sendAndRecvTinyPb()` 可以重新连接并完成请求。
- 重试耗尽后保留 `ERROR_TCP_CONNECT_FAILED` 和包含尝试次数的错误文本。
- `test_tcp_client` 覆盖服务端稍后启动后的重试成功、重试耗尽失败、显式 close 后再次请求成功。

验证命令：

```bash
./build.sh
./build/test_tcp_client
./scripts/check_rpc_sync_basic.sh
```

当前限制：

- 不做后台自动重连。
- 不做连接池。
- 不做多服务节点负载均衡。

### 任务四十四：同步客户端错误码矩阵

已完成能力：

- 整理 `comm/errorcode.h`，按序列化、dispatcher、channel、TcpClient 分组。
- 新增 `docs/error-code.md`，列出错误码、层级、触发场景和测试覆盖。
- 明确 controller error、TinyPB `errCode`、业务 response `ret_code` 三层含义。
- `TcpClient` 写 socket 改为 `send(..., MSG_NOSIGNAL)`，避免对端关闭时 `SIGPIPE` 终止进程。
- `test_tcp_client` 新增 send failed 覆盖 `ERROR_TCP_SEND_FAILED`。

验证命令：

```bash
./build.sh
./build/test_tcp_client
./build/test_tinypb_rpc_channel
./scripts/check_rpc_sync_basic.sh
```

### 任务四十五：同步 RPC 稳定性回归脚本

已完成能力：

- 新增 `scripts/check_rpc_sync.sh`，作为后续阶段必须运行的同步 RPC 回归入口。
- 脚本覆盖构建、TcpBuffer、AbstractCodec、TinyPB data/codec、连接编解码、Protobuf service、dispatcher、msgReq、TcpClient、TinyPbRpcChannel 和真实端到端同步 RPC。
- README 将推荐入口从基础脚本更新为 `./scripts/check_rpc_sync.sh`。
- 阶段 9 文档明确后续阶段完成前需要运行该脚本。

验证命令：

```bash
./scripts/check_rpc_sync.sh
```

### 任务四十六：推迟响应缓存，仅保留 msgReq mismatch 检查

已完成能力：

- 明确同步 RPC 只有单 in-flight request，`TinyPbRpcChannel` 收到一个 response 后立即进行 `msgReq` 校验。
- `msgReq` 不匹配时直接设置 `ERROR_RPC_MSGREQ_MISMATCH`，不反序列化业务 response。
- 同步 `TcpClient` 和 `TinyPbRpcChannel` 文档注释明确不维护 `msgReq -> response` 缓存。
- 阶段 9 文档说明 pending map、乱序响应缓存和迟到响应处理留到异步 RPC 阶段。

验证命令：

```bash
./build/test_tinypb_rpc_channel
./scripts/check_rpc_sync.sh
```

## 阶段 10：Timer、Reactor wakeup 和连接生命周期

### 任务四十七：`TimerEvent` 与基础时间函数

已完成能力：

- 新增 `mytinyrpc/net/timer.h` 和 `mytinyrpc/net/timer.cc`。
- 新增 `getNowMs()`，提供毫秒级时间基准。
- 新增 `TimerEvent`，支持一次性任务、重复任务、cancel、reset 和到期判断。
- 新增 `test_timer_event`，以内存级测试覆盖 TimerEvent 行为，不依赖 Reactor。
- 新增 `docs/stage-10.md` 记录当前边界：本任务不接入 `timerfd`，不接入 TcpConnection。

验证命令：

```bash
./build.sh
./build/test_timer_event
./scripts/check_rpc_sync.sh
```

### 任务四十八：`Timer` + `timerfd` 接入 Reactor

已完成能力：

- 新增 `Timer`，通过 `timerfd_create(CLOCK_MONOTONIC, TFD_NONBLOCK | TFD_CLOEXEC)` 把时间到期转换成 fd 可读事件。
- `Reactor` 构造时持有 `Timer`，并通过 `getTimer()` 提供添加/删除定时任务入口。
- `Timer` 使用 `FdEvent` 注册 timerfd，Reactor 可通过 `epoll_wait()` 唤醒并执行 timer callback。
- `Timer` 添加、删除、执行任务后都会按最近到期任务刷新 `timerfd_settime()`。
- `test_timer` 覆盖一次性任务、重复任务、多个任务按到期时间触发、删除任务后不触发。
- `docs/stage-10.md` 补充 timerfd 触发路径和当前边界。

验证命令：

```bash
./build.sh
./build/test_timer
./build/test_reactor
./scripts/check_rpc_sync.sh
```

### 任务四十九：Reactor 任务队列和 wakeup fd

已完成能力：

- `Reactor` 新增 `addTask()`、`loop()`、`stop()`。
- Reactor 内部使用 `eventfd(EFD_NONBLOCK | EFD_CLOEXEC)` 作为 wakeup fd，并注册到 epoll。
- 其他线程调用 `addTask()` 后会写 eventfd，唤醒阻塞中的 `epoll_wait()`。
- wakeup 回调读取 eventfd 计数并在 Reactor 线程按提交顺序执行任务队列。
- `stop()` 不依赖额外网络事件，可唤醒阻塞中的 loop 并退出。
- `test_reactor` 覆盖跨线程 addTask、任务顺序和 stop 唤醒。

验证命令：

```bash
./build.sh
./build/test_reactor
./build/test_timer
./scripts/check_rpc_sync.sh
```
