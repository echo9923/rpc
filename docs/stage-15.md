# 阶段 15：异步 RPC Channel

阶段 15 的目标是从同步 Stub 调用升级到异步 RPC 调用：先明确 request、response、controller 和 closure 的生命周期，再逐步接入 pending map、IOThread/Reactor、超时和取消。

## 任务七十四：异步 Channel 生命周期外壳

已完成能力：

- 新增 `TinyPbRpcAsyncChannel`，继承 `google::protobuf::RpcChannel`。
- 新增 `AsyncCallContext`，保存本次调用的 `msgReq`、method 全名、controller、request、response 和 closure。
- `CallMethod()` 会在参数合法时生成或复用 controller 中的 `msgReq`。
- 当前外壳内部临时复用 `TinyPbRpcChannel` 完成同步 TinyPB 网络请求。
- 成功路径和失败路径都会由同步 Channel 执行 `done` closure。
- 新增 `test_tinypb_rpc_async_channel`，覆盖成功调用、网络失败仍执行 done、非法参数仍执行 done。

当前调用链：

```mermaid
flowchart LR
    Stub["Protobuf Stub"] --> AsyncChannel["TinyPbRpcAsyncChannel"]
    AsyncChannel --> Context["AsyncCallContext"]
    AsyncChannel --> SyncChannel["TinyPbRpcChannel"]
    SyncChannel --> TcpClient["TcpClient"]
    TcpClient --> TinyPB["TinyPB request/response"]
    SyncChannel --> Done["done closure"]
```

## 当前边界

- 当前还不是真正并发异步网络 IO。
- 当前不维护 pending map。
- 当前不支持乱序响应匹配。
- 当前不做异步超时和取消。
- 当前 `AsyncCallContext` 保存非拥有指针，调用方仍需保证 request、response、controller 和 closure 在 `CallMethod()` 返回前有效。

## 验证命令

```bash
./build.sh
./build/test_tinypb_rpc_async_channel
./build/test_tinypb_rpc_channel
./scripts/check_rpc_sync.sh
```
