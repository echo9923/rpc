# 原 TinyRPC 功能覆盖矩阵

本文用于任务八十三：按原 TinyRPC 风格模块梳理当前 MyTinyRPC 的复刻状态。状态分为：

- 已复刻：核心语义和当前项目主链路可用，并有测试或脚本验证。
- 简化复刻：保留核心思想，但实现边界明显收窄。
- 暂不复刻：当前学习主线不需要，后续如有必要再规划。

本项目不追求与原项目 100% 行为一致；矩阵只用于说明“当前能做什么、哪里简化、如何验证”。

## 覆盖总表

| 原模块 | 当前路径 | 状态 | 当前能力 | 验证方式 |
|---|---|---|---|---|
| `comm/config` | `mytinyrpc/comm/config.*`、`conf/*.xml` | 已复刻核心语义 | 分组式 XML、默认配置、TinyPB/HTTP 协议选择、IOThread、timeout、最大连接超时、RPC/APP 日志配置、协程配置、reqId 长度和 timewheel 配置。 | `./build/test_config`、`./build/test_start`、`scripts/check_generator.sh` |
| `comm/log` | `mytinyrpc/comm/log.*` | 已复刻核心语义 | RPC/APP 双日志、独立级别过滤、`LogEvent`、pid/tid/协程 id、文件行号、函数名、reqId 自动补齐、同步/异步 flush、shutdown drain、按大小滚动。 | `./build/test_log`、`./build/test_runtime` |
| `comm/start` | `mytinyrpc/comm/start.*` | 已复刻核心语义 | `InitConfig()`、`StartRpcServer()`、`GetConfig()`、`GetConstConfig()`、`GetServer()`、`GetIOThreadPoolSize()`、`AddTimerTask()`、TinyPB/HTTP 注册宏。 | `./build/test_start`、`scripts/check_generator_project.sh` |
| `comm/runtime` | `mytinyrpc/comm/runtime.*` | 已复刻核心语义 | 启动期全局 runtime、codec/dispatcher/server 保存、TimerTask 投递、线程局部 request context，覆盖 reqId、interface、method、local/peer 和协议类型。 | `./build/test_runtime`、`./build/test_start` |
| `coroutine` | `mytinyrpc/coroutine/coroutine.*`、`coroutinehook.*` | 已复刻核心语义 | 基础协程对象、外部栈、`yield()`/`resume()`、显式 IO hook、透明 `read/write/accept/connect/sleep/usleep` hook、全局 hook 开关。 | `./build/test_coroutine`、`./build/test_hook`、`./build/test_hook_sleep`、`./build/test_hook_socket`、`./scripts/check_coroutinehook.sh` |
| `coroutinepool` | `mytinyrpc/coroutine/coroutinepool.*` | 已复刻核心语义 | 配置化初始容量、耗尽返回空或按块扩展、协程对象复用、归还状态检查、栈块归还。 | `./build/test_coroutinepool`、`./build/test_coroutine_pool` |
| 协程栈内存池 | `mytinyrpc/coroutine/memory.*` | 已复刻核心语义 | 固定块内存池、归属检查、非法归还防御；`CoroutinePool` 内部协程栈已接入 `FixedMemoryPool`。 | `./build/test_memory_pool`、`./build/test_coroutinepool` |
| `net/reactor` | `mytinyrpc/net/reactor.*`、`fdevent.*` | 已复刻 | epoll fd event、事件注册/删除、task queue、eventfd wakeup、stop、callback 线程归属。 | `./build/test_reactor`、`docs/reactor-event-lifecycle.md` |
| `net/timer` | `mytinyrpc/net/timer.*` | 已复刻 | `TimerTask`、`getNowMs()`、timerfd、一次性/重复定时任务、取消和删除。 | `./build/test_timer_task`、`./build/test_timer` |
| `net/tcp` | `mytinyrpc/net/tcp*.{h,cc}`、`netaddress.*` | 已复刻 | `TcpBuffer`、`TcpClient`、`TcpConnection`、`TcpServer`、同步超时、重连、多 Reactor server、连接空闲超时基础能力。 | `./build/test_tcp_buffer`、`./build/test_tcp_client`、`./build/test_connection_codec`、`scripts/check_rpc_sync.sh`、`scripts/check_stage11_server.sh` |
| `net/http` | `mytinyrpc/net/http/*` | 已复刻核心语义 | HTTP/1.0/1.1 GET/POST、origin-form/absolute-form request target、query map、大小写无关 header、Content-Length body、默认 response header、精确路径/root servlet、错误响应和统一关闭连接。 | `./build/test_http_define`、`./build/test_http_codec`、`./build/test_http_dispatcher`、`scripts/check_stage12_http.sh` |
| `net/tinypb` | `mytinyrpc/net/tinypb/*` | 已复刻 | TinyPB data/codec/dispatcher、Protobuf service 分发、同步 `TinyPbRpcChannel`、controller、reqId、异步 Channel pending/timeout/cancel。 | `./build/test_tinypb_codec`、`./build/test_tinypb_dispatcher`、`./build/test_tinypb_rpc_channel`、`./build/test_tinypb_rpc_async_channel`、`scripts/check_rpc_sync.sh`、`scripts/check_rpc_async.sh` |
| `generator` | `generator/tinyrpc_generator.py`、`generator/template/*` | 已复刻核心工程语义 | CLI、simple/full layout、`protoc` 生成 `.pb.h/.pb.cc` 和 descriptor-set、descriptor service/method 解析、多 service 选择、method interface、service 适配、test client、生成工程 CMake、启动/调用/关闭脚本。 | `scripts/check_generator.sh`、`scripts/check_generator_project.sh` |

## 暂不复刻或后续再评估

| 功能 | 状态 | 当前理由 |
|---|---|---|
| MySQL 插件 | 暂不复刻 | 不属于当前 TinyPB/HTTP/RPC 主链路。 |
| 完整连接池和负载均衡 | 暂不复刻 | 当前同步/异步客户端只覆盖单目标地址，后续可在客户端语义稳定后单独规划。 |
| HTTPS / HTTP/2 / chunked / streaming response | 暂不复刻 | 当前 HTTP 目标是理解 codec/dispatcher/server 闭环。 |
| 完整 tracing 系统 | 暂不复刻 | 当前仅提供 request context 和日志 reqId。 |
| 高性能压测与复杂内存池 | 暂不复刻 | 当前是学习型复刻，先保证行为可解释和脚本可回归。 |
| 生成器高级 proto 语义 | 暂不复刻 | 已使用 descriptor-set 获取 package/service/method/request/response，但不支持 streaming RPC、proto2 特殊语义或多语言生成。 |

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

当前项目已经覆盖 TinyRPC 学习主线中的配置、日志、启动入口、运行时、Reactor、Timer、TCP、TinyPB、HTTP、协程、异步 RPC 和生成器。阶段 23 后，生成器已经具备原项目风格目录、`protoc` 流程、descriptor 解析、interface/service/test_client 模板和端到端生成工程验收。主要简化点集中在生产级扩展能力：连接池、tracing、插件系统、性能优化、高级 proto 语义和发布级独立工程打包。
