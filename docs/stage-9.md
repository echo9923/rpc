# 阶段 9：同步客户端连接语义收口

阶段 9 的目标是让同步 RPC 客户端的连接失败、关闭、重连、超时和错误码边界可解释、可回归。

## 任务四十三：`TcpClient` 重连和关闭边界

已完成能力：

- `closeConnection()` 后，下一次 `sendTinyPbRequest()` / `sendAndRecvTinyPb()` 会重新创建 fd 并连接。
- 连接失败后会关闭失败 fd，下一次尝试重新创建 fd。
- 新增 `setConnectRetry(retryCount, retryIntervalMs)`：
  - `retryCount` 表示失败后的额外重试次数，0 表示不重试。
  - `retryIntervalMs` 表示两次尝试之间的等待时间，单位毫秒。
- 重试耗尽后保留明确错误码和可观察错误文本。

## 当前重连语义

- 重试只发生在显式调用 `connectServer()` 或发送请求时，不做后台自动重连。
- 每次失败都会关闭当前 fd，下一次尝试重新调用 `socket()`。
- 成功连接后不会继续重试。
- 主动 `closeConnection()` 是幂等操作。

## 当前限制

- 不做连接池。
- 不做多服务节点负载均衡。
- 不做后台健康检查。
- 不缓存待发送请求。

## 验证命令

```bash
./build.sh
./build/test_tcp_client
./scripts/check_rpc_sync.sh
```

## 任务四十四：同步客户端错误码矩阵

已补充 [错误码说明](error-code.md)，明确三层错误承载位置：

- controller error：客户端框架层错误。
- TinyPB `errCode`：服务端框架层错误。
- business `ret_code`：业务 response 中的业务结果。

`ERROR_TCP_SEND_FAILED` 现在通过 `TcpClientTest.SendTinyPbRequestFailsWhenPeerResetsConnection` 覆盖。客户端写入 socket 时使用 `send(..., MSG_NOSIGNAL)`，避免对端关闭后触发 `SIGPIPE` 终止进程。

## 任务四十五：同步 RPC 稳定性回归脚本

新增 `scripts/check_rpc_sync.sh` 作为后续阶段的同步 RPC 稳定性安全网。脚本会先构建项目，再串联运行同步 RPC 主链路相关测试：

- `test_tcp_buffer`
- `test_abstract_codec`
- `test_tinypb_data`
- `test_tinypb_codec`
- `test_connection_codec`
- `test_protobuf_service`
- `test_tinypb_dispatcher`
- `test_msg_req`
- `test_tcp_client`
- `test_tinypb_rpc_channel`
- `scripts/check_stage8_rpc.sh`

后续 Reactor、IOThread、HTTP、配置日志、协程整理和异步 RPC 阶段完成前，都应运行：

```bash
./scripts/check_rpc_sync.sh
```

验收通过以输出 `[rpc-sync] PASS` 为准。
