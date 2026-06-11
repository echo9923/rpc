# MyTinyRPC

MyTinyRPC is a staged TinyRPC learning project. It now covers the main learning path from a blocking TCP Echo Server to Reactor/Timer, multi-Reactor TCP server, TinyPB synchronous and asynchronous RPC, HTTP server support, runtime startup helpers, coroutine hooks, a full generated-project workflow, optional production-shell utilities, and regression scripts.

## Quick Start

Run everything in Linux/WSL:

```bash
./scripts/check_all.sh
```

From Windows PowerShell:

```powershell
.\scripts\check_all.ps1
```

Expected final output:

```text
[all] PASS
```

Run the stage 25 full-completion gate when you want to verify the completed supplement plan:

```bash
./scripts/check_full_completion.sh
```

From Windows PowerShell:

```powershell
.\scripts\check_all.ps1 -FullCompletion
```

Expected final output:

```text
[full-completion] PASS
```

Run the stage 26 server lifecycle gate when you only need to verify framework-level server stop:

```bash
./scripts/check_stage26_lifecycle.sh
```

Expected final output:

```text
[stage26-lifecycle] PASS
```

Run the stage 27 RPC Channel API gate when you need to verify the public synchronous/asynchronous Channel usage surface:

```bash
MYTINYRPC_SKIP_BUILD=1 ./scripts/check_rpc_sync.sh
MYTINYRPC_SKIP_BUILD=1 ./scripts/check_rpc_async.sh
./scripts/check_generator.sh
```

Run the stage 31 generated release package gate when you need to verify a generated project that builds without `MYTINYRPC_ROOT`:

```bash
./scripts/check_generator_release_package.sh
```

Expected final output:

```text
[generator-release] PASS
```

If you only need a fast build:

```bash
./build.sh
```

Install WSL dependencies if needed:

```bash
sudo apt update
sudo apt install -y build-essential cmake netcat-openbsd curl libgtest-dev protobuf-compiler libprotobuf-dev
```

## Current Structure

See [项目目录与命名整理](docs/project-structure.md) for the current module layout, script entries, and naming boundaries.

Core directories:

- `mytinyrpc/comm`: config, log, runtime, startup, error code, and request id.
- `mytinyrpc/net`: Reactor, Timer, TCP client/server, IOThread, HTTP, and TinyPB.
- `mytinyrpc/coroutine`: coroutine, hook, pool, and fixed memory pool.
- `generator`: TinyRPC generated-project CLI and templates, including simple/full layouts, protoc integration, and release package mode.
- `testcases`: unit tests and script-driven acceptance programs.
- `scripts`: stage checks and regression scripts.
- `examples`: runnable example notes for sync RPC, async RPC, HTTP server, and generated projects.

## Core Examples

| Example | Entry |
|---|---|
| TinyPB sync RPC | [examples/tinypb_sync/README.md](examples/tinypb_sync/README.md) |
| TinyPB async RPC | [examples/tinypb_async/README.md](examples/tinypb_async/README.md) |
| HTTP server | [examples/http_server/README.md](examples/http_server/README.md) |
| Generated project | [examples/generated_project/README.md](examples/generated_project/README.md) |

## Completed Supplement Capabilities

Stages 18 to 25 close the earlier simplified areas without changing the project into a commercial RPC distribution. The completed supplement path now includes:

- Config/log/start/runtime completion: grouped XML config, dual RPC/APP logs, runtime request context, and startup helper tests.
- Coroutine completion: transparent `read/write/accept/connect/sleep/usleep` hooks, hook fd ownership, coroutine pool expansion, and fixed stack memory pool coverage.
- Client networking completion: `TcpClient` uses client-side `TcpConnection`, Reactor/Timer timeout handling, response matching, connection reuse, explicit close, and failure rebuild.
- Async RPC completion: `TinyPbRpcAsyncChannel` uses a long-lived `AsyncClientSession`, nonblocking socket, EPOLLIN/EPOLLOUT, pending matching, timeout/cancel/stop arbitration, and real `TcpServer` E2E validation.
- RPC Channel API alignment: synchronous and asynchronous Channels now expose `Ptr`, `shared_ptr<IPAddress>` constructors, Channel-level address observation, synchronous timeout/reuse controls, asynchronous `saveCallee()` lifetime holding, and `wait()` / `waitFor()` completion waits.
- HTTP completion: HTTP/1.0/1.1 GET/POST, origin-form and absolute-form targets, query map, case-insensitive headers, `Content-Length` body, default response headers, root servlet, 404/500 responses, request context, and close-after-response semantics.
- Generator completion: simple/full layouts, `protoc`, descriptor-set parsing, package-aware service/method selection, method interface classes, service adapters, generated test client, source/release package modes, and generated-project build/start/call/shutdown verification.
- Optional shell capabilities: default-off MySQL plugin skeleton, fixed `ThreadPool`, lightweight tracing fields, basic benchmark programs, and resource-lifetime checks.
- Server lifecycle completion: `TcpServer::stop()` wakes blocking `start()`, shuts down the listen fd, clears active connections, stops IOThreadPool, releases the port, and is available through `StopRpcServer()`.

The complete supplement gate is:

```bash
./scripts/check_full_completion.sh
```

It ends with `[full-completion] PASS`.

## Regression Scripts

| Script | Purpose |
|---|---|
| `scripts/check_rpc_sync.sh` | Synchronous TinyPB RPC safety net. |
| `scripts/check_rpc_async.sh` | Asynchronous TinyPB RPC lifecycle and timeout/cancel regression. |
| `scripts/check_stage11_server.sh` | Multi-Reactor TinyPB server regression. |
| `scripts/check_stage12_http.sh` | HTTP server regression. |
| `scripts/check_stage26_lifecycle.sh` | Stage 26 TcpServer stop and lifecycle regression. |
| `scripts/check_generator.sh` | Generator template and service/method skeleton regression. |
| `scripts/check_generator_project.sh` | Generated project build/start/client/shutdown regression. |
| `scripts/check_generator_release_package.sh` | Generated release package build/start/client/shutdown regression without `MYTINYRPC_ROOT`. |
| `scripts/check_resource_lifetime.sh` | Stage 24 benchmark and resource-lifetime regression. |
| `scripts/check_all.sh` | Full local regression across the project. |
| `scripts/check_full_completion.sh` | Stage 25 full-completion gate across all supplement-stage regressions. |

## Minimal Runtime Entry

TinyPB server:

```cpp
#include "comm/start.h"

int main()
{
    tinyrpc::InitConfig("conf/test_tinypb_server.xml");
    tinyrpc::StartRpcServer();
    REGISTER_SERVICE(QueryServiceImpl);
    auto server = tinyrpc::GetServer();
    server->start();
}
```

HTTP server:

```cpp
#include "comm/start.h"

int main()
{
    tinyrpc::InitConfig("conf/test_http_server.xml");
    tinyrpc::StartRpcServer();
    REGISTER_HTTP_SERVLET("/hello", HelloServlet);
    auto server = tinyrpc::GetServer();
    server->start();
}
```

`StartRpcServer()` creates and initializes the server from XML. `GetServer()->start()` enters the blocking event loop after services or servlets have been registered. `GetServer()->stop()` or `StopRpcServer()` can be called from another thread or a signal handler to wake the loop and shut down the server lifecycle.

## Documentation

- [复刻进度](docs/replica-progress.md)
- [简化实现补全任务计划书](docs/simplified-completion-task-plan.md)
- [学习总结](docs/learning-summary.md)
- [原 TinyRPC 功能覆盖矩阵](docs/original-coverage-matrix.md)
- [错误码说明](docs/error-code.md)
- [Reactor 事件生命周期](docs/reactor-event-lifecycle.md)
- [TcpConnection 生命周期](docs/tcpconnection-lifetime.md)
- [协程模型](docs/coroutine-model.md)
- [代码生成器与示例工程](docs/stage-16.md)
- [生成器完整化](docs/stage-23.md)
- [可选插件、观测和性能边界](docs/stage-24.md)
- [TcpServer 优雅停止和生命周期收口](docs/stage-26.md)
- [RPC Channel API 对齐](docs/stage-27.md)
- [发布级生成工程打包](docs/stage-31.md)

## Current Boundaries

- This is a learning implementation, not a production RPC distribution.
- HTTP is a minimal request/response server path; HTTPS, HTTP/2, chunked, and streaming are out of scope.
- Generated projects support both source-tree mode through `MYTINYRPC_ROOT` and stage 31 release package mode through bundled `third_party/mytinyrpc` source.
- `TcpServer::start()` is blocking; stage 26 adds framework-level `TcpServer::stop()` / `StopRpcServer()` for graceful shutdown, while scripts still use pid files as their user-facing process handle.
- Connection pools, load balancing, MySQL plugins, full tracing, and performance reports are intentionally not part of the current scope.

## Editor Note

When using a C++ language server, open the repository in the same Linux/WSL environment used for builds. If `build/compile_commands.json` contains paths from another environment, remove `build/` and rerun:

```bash
rm -rf build
bash build.sh
```
