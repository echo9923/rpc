# 阶段 27：RPC Channel API 对齐

阶段 27 的目标是重新对齐 RPC Channel 的外部使用体验，让业务代码和生成工程更接近原 TinyRPC 的调用姿势，同时保留当前项目已经补齐的 Reactor 化同步客户端、异步 pending 仲裁、timeout/cancel/stop 和 request context 能力。

## 任务一百二十八：对齐同步 RPC Channel API

已完成能力：

- `TinyPbRpcChannel` 新增 `using Ptr = std::shared_ptr<TinyPbRpcChannel>`，业务代码可以直接使用 `TinyPbRpcChannel::Ptr` 持有 Channel。
- `TinyPbRpcChannel` 新增 `std::shared_ptr<IPAddress>` 构造入口，对齐原 TinyRPC 中以共享地址对象创建 Channel 的使用方式。
- 同步 Channel 内部持有长生命周期 `TcpClient`，不再每次 `CallMethod()` 都临时创建客户端对象。
- Channel 层新增 `setTimeout()` / `getTimeout()`，作为所有同步调用的默认网络超时；单次调用仍可用 `TinyPbRpcController::setTimeout()` 覆盖。
- Channel 层新增 `setReuseConnection()` / `isReuseConnection()` 和 `closeConnection()`，可以显式控制同一个 Channel 下多次 Stub 调用是否复用 TCP 连接。
- `test_tinypb_rpc_channel` 新增 `TinyPbRpcChannel::Ptr`、`shared_ptr<IPAddress>` 和连续两次 Stub 调用复用同一连接的回归用例。

验证命令：

```bash
cmake --build build --target test_tinypb_rpc_channel -j2
./build/test_tinypb_rpc_channel
```

## 任务一百二十九：对齐异步 RPC Channel API

已完成能力：

- `TinyPbRpcAsyncChannel` 新增 `Ptr`、`ControllerPtr`、`MessagePtr` 和 `ClosurePtr` 类型别名。
- `TinyPbRpcAsyncChannel` 新增 `std::shared_ptr<IPAddress>` 构造入口，和同步 Channel 保持一致。
- 新增 `saveCallee()`，用于在下一次 Stub 调用前保存 controller、request、response 和 closure 的 `shared_ptr` 生命周期。
- 新增 `wait()` 和 `waitFor()`，等待当前 Channel 的 pending 请求清空，并等待正在执行的完成回调退出。
- 新增 `getPeerAddress()`，方便生成代码、测试和用户代码观察当前 Channel 目标地址。
- `test_tinypb_rpc_async_channel` 新增 `saveCallee()` + `waitFor()` 的真实 TinyPB 服务端回调用例，验证异步调用完成后 response 已写回、closure 已执行、pending 已清空。

验证命令：

```bash
cmake --build build --target test_tinypb_rpc_async_channel -j2
./build/test_tinypb_rpc_async_channel
MYTINYRPC_SKIP_BUILD=1 ./scripts/check_rpc_async.sh
```

## 任务一百三十：生成客户端和文档收口

已完成能力：

- `generator/template/client.cc.template` 使用 `std::make_shared<IPAddress>` 和 `TinyPbRpcChannel::Ptr` 创建同步 RPC Channel。
- 生成客户端显式调用 `channel->setReuseConnection(true)`，让生成工程示例使用阶段 27 对齐后的 Channel 级连接复用入口。
- `scripts/check_generator.sh` 增加对 `TinyPbRpcChannel::Ptr` 和 `setReuseConnection(true)` 的断言，覆盖 simple/full 两种布局。
- README、学习总结、覆盖矩阵、项目结构和复制进度文档同步记录阶段 27 状态。

验证命令：

```bash
./scripts/check_generator.sh
./scripts/check_generator_project.sh
MYTINYRPC_SKIP_BUILD=1 ./scripts/check_rpc_sync.sh
MYTINYRPC_SKIP_BUILD=1 ./scripts/check_rpc_async.sh
```

## 当前边界

- `TinyPbRpcChannel` 仍然是单目标同步 Channel，不实现多后端选择、服务发现或负载均衡。
- 同步 Channel 的连接复用由 `TcpClient` 承担；失败后重建和响应匹配仍沿用阶段 20 的客户端语义。
- `TinyPbRpcAsyncChannel::saveCallee()` 是生命周期托管入口，不改变 Protobuf Stub 的 `CallMethod()` 标准调用协议。
- 异步 Channel 仍使用单个 `AsyncClientSession` 和一个内部 `IOThread`，不在本阶段扩展为连接池。
- 生成工程的 source 模式仍可依赖 `MYTINYRPC_ROOT` 指向本地 MyTinyRPC 源码树；阶段 31 已补齐 `--package release` 发布级源码包模式。
