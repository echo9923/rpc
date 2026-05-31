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
