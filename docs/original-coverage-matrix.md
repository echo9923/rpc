# 原 TinyRPC 功能覆盖矩阵

本文用于任务八十三：按原 TinyRPC 风格模块梳理当前 MyTinyRPC 的复刻状态。状态分为：

- 已复刻：核心语义和当前项目主链路可用，并有测试或脚本验证。
- 简化复刻：保留核心思想，但实现边界明显收窄。
- 暂不复刻：当前学习主线不需要，后续如有必要再规划。

本项目不追求与原项目 100% 行为一致；矩阵只用于说明“当前能做什么、哪里简化、如何验证”。

## 覆盖总表

| 原模块 | 当前路径 | 状态 | 当前能力 | 验证方式 |
|---|---|---|---|---|
| `comm/config` | `mytinyrpc/comm/config.*`、`conf/*.xml` | 简化复刻 | 默认配置、XML 读取、TinyPB/HTTP 协议选择、IOThread 数量、timeout、log level。 | `./build/test_config`、`./build/test_start` |
| `comm/log` | `mytinyrpc/comm/log.*` | 简化复刻 | 同步文件日志、级别过滤、线程 id、文件行号、reqId、flush、关闭输出、简化异步队列。 | `./build/test_log`、`./build/test_runtime` |
| `comm/start` | `mytinyrpc/comm/start.*` | 简化复刻 | `InitConfig()`、`StartRpcServer()`、`GetServer()`、TinyPB/HTTP 注册宏。 | `./build/test_start`、`scripts/check_generator_project.sh` |
| `comm/runtime` | `mytinyrpc/comm/runtime.*` | 简化复刻 | 启动期全局 runtime、codec/dispatcher/server 保存、线程局部 request context。 | `./build/test_runtime`、`./build/test_start` |
| `coroutine` | `mytinyrpc/coroutine/coroutine.*`、`coroutinehook.*` | 简化复刻 | 基础协程对象、`Yield()`/`resume()`、read/write/connect/sleep/usleep/recv/send/accept hook。 | `./build/test_coroutine`、`./build/test_hook`、`./build/test_hook_sleep`、`./build/test_hook_socket` |
| `coroutinepool` | `mytinyrpc/coroutine/coroutinepool.*` | 简化复刻 | 固定容量协程复用、耗尽返回空、归还状态检查。 | `./build/test_coroutinepool` |
| 协程栈内存池 | `mytinyrpc/coroutine/memory.*` | 简化复刻 | 固定块内存池、归属检查、非法归还防御；暂未强制接入 `Coroutine` 栈。 | `./build/test_memory_pool` |
| `net/reactor` | `mytinyrpc/net/reactor.*`、`fdevent.*` | 已复刻 | epoll fd event、事件注册/删除、task queue、eventfd wakeup、stop、callback 线程归属。 | `./build/test_reactor`、`docs/reactor-event-lifecycle.md` |
| `net/timer` | `mytinyrpc/net/timer.*` | 已复刻 | `TimerTask`、`getNowMs()`、timerfd、一次性/重复定时任务、取消和删除。 | `./build/test_timer_task`、`./build/test_timer` |
| `net/tcp` | `mytinyrpc/net/tcp*.{h,cc}`、`netaddress.*` | 已复刻 | `TcpBuffer`、`TcpClient`、`TcpConnection`、`TcpServer`、同步超时、重连、多 Reactor server、连接空闲超时基础能力。 | `./build/test_tcp_buffer`、`./build/test_tcp_client`、`./build/test_connection_codec`、`scripts/check_rpc_sync.sh`、`scripts/check_stage11_server.sh` |
| `net/http` | `mytinyrpc/net/http/*` | 简化复刻 | HTTP request/response、GET/POST request decode、response encode、精确路径 servlet dispatcher、HTTP server 验收。 | `./build/test_httpdefine`、`./build/test_http_codec`、`./build/test_http_dispatcher`、`scripts/check_stage12_http.sh` |
| `net/tinypb` | `mytinyrpc/net/tinypb/*` | 已复刻 | TinyPB data/codec/dispatcher、Protobuf service 分发、同步 `TinyPbRpcChannel`、controller、reqId、异步 Channel pending/timeout/cancel。 | `./build/test_tinypb_codec`、`./build/test_tinypb_dispatcher`、`./build/test_tinypb_rpc_channel`、`./build/test_tinypb_rpc_async_channel`、`scripts/check_rpc_sync.sh`、`scripts/check_rpc_async.sh` |
| `generator` | `generator/tinyrpc_generator.py`、`generator/template/*` | 简化复刻 | CLI、模板复制、简单 service/method 解析、接口骨架、生成工程 CMake、启动/调用/关闭脚本。 | `scripts/check_generator.sh`、`scripts/check_generator_project.sh` |

## 暂不复刻或后续再评估

| 功能 | 状态 | 当前理由 |
|---|---|---|
| MySQL 插件 | 暂不复刻 | 不属于当前 TinyPB/HTTP/RPC 主链路。 |
| 完整连接池和负载均衡 | 暂不复刻 | 当前同步/异步客户端只覆盖单目标地址，后续可在客户端语义稳定后单独规划。 |
| HTTPS / HTTP/2 / chunked / streaming response | 暂不复刻 | 当前 HTTP 目标是理解 codec/dispatcher/server 闭环。 |
| 完整 tracing 系统 | 暂不复刻 | 当前仅提供 request context 和日志 reqId。 |
| 高性能压测与复杂内存池 | 暂不复刻 | 当前是学习型复刻，先保证行为可解释和脚本可回归。 |
| 生成器完整 Protobuf parser | 暂不复刻 | 当前只支持简单 service block 和一元 rpc method，足够生成示例工程。 |

## 回归建议

任务八十四会新增一键全量回归脚本。在脚本完成前，建议按下列顺序验证核心覆盖面：

```bash
./build.sh
./scripts/check_rpc_sync.sh
./scripts/check_stage11_server.sh
./scripts/check_stage12_http.sh
./scripts/check_rpc_async.sh
./scripts/check_generator_project.sh
```

## 结论

当前项目已经覆盖 TinyRPC 学习主线中的配置、日志、启动入口、运行时、Reactor、Timer、TCP、TinyPB、HTTP、协程、异步 RPC 和生成器。主要简化点集中在生产级工程能力：复杂配置 schema、完整 HTTP 协议、连接池、tracing、插件系统、性能优化和完整代码生成器。
