# 项目目录与命名整理

本文记录任务八十二后的目录结构、脚本入口和当前保留的命名边界。目标是让后续维护者能快速判断文件应该放在哪里，而不是依赖历史提交猜测。

## 顶层目录

| 目录 | 作用 |
|---|---|
| `mytinyrpc/comm` | 配置、日志、错误码、请求号、运行时上下文和启动入口。 |
| `mytinyrpc/coroutine` | 协程对象、hook、协程池和固定块内存池。 |
| `mytinyrpc/net` | fd、Reactor、Timer、TcpBuffer、TcpClient、TcpServer、IOThread 和连接生命周期。 |
| `mytinyrpc/net/http` | HTTP request/response、codec、servlet 和 dispatcher。 |
| `mytinyrpc/net/tinypb` | TinyPB data/codec/dispatcher、同步 RPC Channel、异步 RPC Channel 和 controller。 |
| `generator` | TinyRPC 业务工程生成器和模板。 |
| `conf` | 测试和示例配置文件。 |
| `testcases` | 单元测试、脚本验收程序和端到端测试入口。 |
| `scripts` | 阶段验收、同步/异步 RPC 回归、HTTP 回归和生成器验收脚本。 |
| `docs` | 阶段文档、调试文档、错误码说明、进度记录和任务计划。 |

## 脚本入口

| 脚本 | 作用 |
|---|---|
| `scripts/check_stage1.sh` | 阶段 1 阻塞 Echo Server 验收。 |
| `scripts/check_stage8_rpc.sh` | 阶段 8 Stub 到真实 TcpServer 的同步 RPC 验收。 |
| `scripts/check_rpc_sync.sh` | 同步 TinyPB RPC 稳定性回归入口。 |
| `scripts/check_stage11_server.sh` | 多 Reactor TcpServer 同步 RPC 验收。 |
| `scripts/check_stage12_http.sh` | HTTP server 验收。 |
| `scripts/check_rpc_async.sh` | 异步 TinyPB RPC 回归入口。 |
| `scripts/check_generator.sh` | 生成器模板、proto service/method 骨架和编译校验。 |
| `scripts/check_generator_project.sh` | 生成工程端到端构建、启动、调用和关闭验收。 |

## 命名整理

- 新增测试文件优先使用 `test_*.cc`，并放在 `testcases/`。
- 早期 `testtcpchoserver.cc` 已整理为 `test_tcp_echo_server.cc`，CMake 目标名保持 `test_tcp_echo_server`。
- C++ 源码文件继续沿用当前目录内既有小写命名，例如 `tcpserver.cc`、`tinypbcodec.cc`。本任务不做大规模 API 或 include 改名。
- 生成器模板统一放在 `generator/template/`，模板文件以 `.template` 结尾，生成时去掉该后缀。

## 当前保留边界

- `mytinyrpc` 目录名保留不变，避免无意义的大规模 include 迁移。
- `TcpServer` 当前仍是阻塞式 `start()`，没有框架层 `stop()`；需要脚本验收时由脚本管理后台进程。
- `build/` 是跨环境产物目录，切换 Windows/WSL/Docker 构建环境后应先清理再重建。
