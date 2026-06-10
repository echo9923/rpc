# 原 TinyRPC 功能覆盖矩阵

本文用于阶段 25 收口：用矩阵说明当前 MyTinyRPC 对原 TinyRPC 学习对象的覆盖程度、补全来源和验证入口。状态统一分为：

- 已复刻：核心语义和当前项目主链路完整可用，并有测试或脚本验证。
- 已复刻核心语义：保留原模块关键学习语义，生产级扩展能力仍有明确边界。
- 保留简化：当前只保留学习项目需要的最小能力，不继续在本阶段补全。
- 可选未启用：默认构建不启用，但有配置、编译或 no-op 骨架。

本项目不追求与原项目 100% 行为一致；矩阵只回答“哪些简化实现已经补全、由哪个任务补全、如何验证”。

## 覆盖总表

| 原模块 | 当前路径 | 状态 | 补全任务编号 | 当前能力 | 验证方式 |
|---|---|---|---|---|---|
| `comm/config` | `mytinyrpc/comm/config.*`、`conf/*.xml` | 已复刻核心语义 | 86、87、90、117 | 分组式 XML、默认配置、TinyPB/HTTP 协议选择、IOThread、timeout、最大连接超时、RPC/APP 日志配置、协程配置、reqId 长度、timewheel 配置和 MySQL 可选配置段。 | `./build/test_config`、`./build/test_start`、`./scripts/check_generator.sh` |
| `comm/log` | `mytinyrpc/comm/log.*` | 已复刻核心语义 | 88、89、118、119 | RPC/APP 双日志、独立级别过滤、`LogEvent`、pid/tid/协程 id、文件行号、函数名、traceId/reqId/interface/method/path/peer/protocol 自动补齐、同步/异步 flush、shutdown drain、按大小滚动和日志生命周期检查。 | `./build/test_log`、`./build/test_runtime`、`./scripts/check_resource_lifetime.sh` |
| `comm/start` | `mytinyrpc/comm/start.*` | 已复刻核心语义 | 90、115 | `InitConfig()`、`StartRpcServer()`、`GetConfig()`、`GetConstConfig()`、`GetServer()`、`GetIOThreadPoolSize()`、`AddTimerTask()`、TinyPB/HTTP 注册宏，生成工程也走同一启动入口。 | `./build/test_start`、`./scripts/check_generator_project.sh` |
| `comm/runtime` | `mytinyrpc/comm/runtime.*` | 已复刻核心语义 | 90、118 | 启动期全局 runtime、codec/dispatcher/server 保存、TimerTask 投递、线程局部 request context，覆盖 traceId、reqId、interface、method、path、local/peer 和协议类型。 | `./build/test_runtime`、`./build/test_tinypb_rpc_channel`、`./build/test_tinypb_rpc_async_channel` |
| `comm/thread_pool` | `mytinyrpc/comm/thread_pool.*` | 已复刻核心语义 | 116、119 | 固定数量普通工作线程、任务队列、stop drain、析构 join 和 stop 后拒绝新任务；用于补齐普通后台 worker 学习对象，不替换 `IOThreadPool`。 | `./build/test_thread_pool`、`./scripts/check_resource_lifetime.sh` |
| `comm/mysql` | `mytinyrpc/comm/mysql_instance.*` | 可选未启用 | 117 | `MYTINYRPC_ENABLE_MYSQL` 默认关闭；关闭时提供 no-op 线程局部实例入口；开启时查找 MySQL/MariaDB client 开发库并编译 wrapper。 | `./build/test_config`、`cmake -S . -B build-mysql-check -DMYTINYRPC_ENABLE_MYSQL=ON` |
| `coroutine` | `mytinyrpc/coroutine/coroutine.*`、`coroutinehook.*` | 已复刻核心语义 | 91、92、95 | 基础协程对象、外部栈、`yield()`/`resume()`、显式 IO hook、透明 `read/write/accept/connect/sleep/usleep` hook、全局 hook 开关和 hook fd 归属整理。 | `./build/test_coroutine`、`./build/test_hook`、`./build/test_hook_sleep`、`./build/test_hook_socket`、`./scripts/check_coroutinehook.sh` |
| `coroutinepool` | `mytinyrpc/coroutine/coroutinepool.*` | 已复刻核心语义 | 93、95 | 配置化初始容量、耗尽返回空或按块扩展、协程对象复用、归还状态检查和回归脚本覆盖。 | `./build/test_coroutinepool`、`./build/test_coroutine_pool`、`./scripts/check_coroutinehook.sh` |
| 协程栈内存池 | `mytinyrpc/coroutine/memory.*` | 已复刻核心语义 | 94、95 | 固定块内存池、归属检查、非法归还防御；`CoroutinePool` 内部协程栈已接入 `FixedMemoryPool`。 | `./build/test_memory_pool`、`./build/test_coroutinepool`、`./scripts/check_coroutinehook.sh` |
| `net/reactor` | `mytinyrpc/net/reactor.*`、`fdevent.*` | 已复刻 | 96、97、100、101、102、103 | epoll fd event、事件注册/删除、task queue、eventfd wakeup、stop、callback 线程归属；同步客户端和异步 Channel 均已接入 Reactor 网络路径。 | `./build/test_reactor`、`./scripts/check_rpc_client_reactor.sh`、`./scripts/check_rpc_async.sh` |
| `net/timer` | `mytinyrpc/net/timer.*` | 已复刻 | 97、104 | `TimerTask`、`getNowMs()`、timerfd、一次性/重复定时任务、取消和删除；同步客户端超时和异步 timeout/cancel 都走 Timer 仲裁。 | `./build/test_timer_task`、`./build/test_timer`、`./scripts/check_rpc_client_reactor.sh`、`./scripts/check_rpc_async.sh` |
| `net/tcp` | `mytinyrpc/net/tcp*.{h,cc}`、`netaddress.*` | 已复刻 | 96、97、98、99、100、105、119 | `TcpBuffer`、`TcpClient`、`TcpConnection`、`TcpServer`、同步超时、响应缓存、重连、连接复用、真实异步网络会话、多 Reactor server、连接空闲超时基础能力和 fd 生命周期检查。 | `./build/test_tcp_buffer`、`./build/test_tcp_client`、`./build/test_connection_codec`、`./scripts/check_rpc_sync.sh`、`./scripts/check_rpc_client_reactor.sh`、`./scripts/check_stage11_server.sh`、`./scripts/check_resource_lifetime.sh` |
| `net/http` | `mytinyrpc/net/http/*` | 已复刻核心语义 | 106、107、108、109、110、118、119 | HTTP/1.0/1.1 GET/POST、origin-form/absolute-form request target、query map、大小写无关 header、Content-Length body、默认 response header、精确路径/root servlet、错误响应、统一关闭连接、HTTP request context 和基础 benchmark。 | `./build/test_http_define`、`./build/test_http_codec`、`./build/test_http_dispatcher`、`./scripts/check_stage12_http.sh`、`./scripts/check_resource_lifetime.sh` |
| `net/tinypb` | `mytinyrpc/net/tinypb/*` | 已复刻 | 96、98、101、102、103、104、105、118、119 | TinyPB data/codec/dispatcher、Protobuf service 分发、同步 `TinyPbRpcChannel`、controller、reqId、响应匹配、异步 Channel pending/timeout/cancel/stop、真实异步网络路径、request context 和 sync/async benchmark。 | `./build/test_tinypb_codec`、`./build/test_tinypb_dispatcher`、`./build/test_tinypb_rpc_channel`、`./build/test_tinypb_rpc_async_channel`、`./scripts/check_rpc_sync.sh`、`./scripts/check_rpc_async.sh`、`./scripts/check_resource_lifetime.sh` |
| `generator` | `generator/tinyrpc_generator.py`、`generator/template/*` | 已复刻核心工程语义 | 111、112、113、114、115 | CLI、simple/full layout、`protoc` 生成 `.pb.h/.pb.cc` 和 descriptor-set、descriptor service/method 解析、多 service 选择、method interface、service 适配、business exception、test client、生成工程 CMake、启动/调用/关闭脚本。 | `./scripts/check_generator.sh`、`./scripts/check_generator_project.sh` |
| benchmark / resource | `testcases/benchmark_*.cc`、`scripts/check_resource_lifetime.sh` | 已复刻核心语义 | 119 | HTTP/TinyPB sync/TinyPB async 基础参考 benchmark、日志和线程池生命周期、真实 TinyPB server fd 增长和残留进程检查。 | `./scripts/check_resource_lifetime.sh` |

## 保留边界

| 功能 | 状态 | 边界说明 | 未来处理口径 |
|---|---|---|---|
| MySQL 真实连接池 | 可选未启用 | 当前只提供可选编译骨架、配置解析和线程局部 no-op/实例入口，不在默认回归中连接真实 MySQL server。 | 需要数据库生命周期管理时另起插件计划。 |
| 完整连接池和负载均衡 | 保留简化 | 当前同步/异步客户端覆盖单目标地址、连接复用和失败重建，不做多后端选择。 | 需要服务发现或多目标 RPC 时单独规划。 |
| HTTPS / HTTP/2 / chunked / streaming response | 保留简化 | 当前 HTTP 目标是理解 codec/dispatcher/server 闭环和 HTTP/1.x 基础请求响应语义。 | 需要更完整 HTTP 能力时另起 HTTP 阶段。 |
| 完整 tracing 系统 | 保留简化 | 当前已提供轻量线程局部 request context 和日志字段补齐，但不实现 OpenTelemetry 或跨进程 trace 传播。 | 需要分布式观测时新建 tracing 计划。 |
| 商业级压测与复杂性能报告 | 保留简化 | 当前只提供基础 benchmark 和资源生命周期检查，先保证行为可解释和脚本可回归。 | 有明确指标后再引入独立压测方案。 |
| 生成器高级 proto 语义 | 保留简化 | 已使用 descriptor-set 获取 package/service/method/request/response，但不支持 streaming RPC、proto2 特殊语义或多语言生成。 | 需要真实 IDL 平台能力时另起生成器计划。 |
| 发布级独立工程打包 | 保留简化 | full layout 生成原项目风格目录，但仍依赖本地 MyTinyRPC 源码路径和脚本 pid 管理。 | 需要发布形态时单独补安装、打包和 server stop 能力。 |

## 回归建议

任务一百二十一会新增完整补全回归脚本。脚本完成前，可以按下面的已有入口验证补全覆盖面：

```bash
./scripts/check_all.sh
./scripts/check_coroutinehook.sh
./scripts/check_rpc_client_reactor.sh
./scripts/check_rpc_async.sh
./scripts/check_stage12_http.sh
./scripts/check_generator_project.sh
./scripts/check_resource_lifetime.sh
```

## 结论

当前项目已经覆盖 TinyRPC 学习主线中的配置、日志、启动入口、运行时、Reactor、Timer、TCP、TinyPB、HTTP、协程、异步 RPC 和生成器。阶段 18 到阶段 24 已经把早期简化项集中补齐：配置/日志/运行时、协程 hook、TcpClient Reactor 化、真实异步 RPC 网络路径、HTTP 协议语义、生成器完整工程、可选插件、轻量观测、基础 benchmark 和资源生命周期脚本均有验证入口。剩余边界属于明确保留的生产级扩展能力，不是阶段 25 的遗漏。
