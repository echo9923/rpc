# MyTinyRPC

MyTinyRPC is a staged TinyRPC learning project. It now covers the main learning path from a blocking TCP Echo Server to Reactor/Timer, multi-Reactor TCP server, TinyPB synchronous and asynchronous RPC, HTTP server support, runtime startup helpers, coroutine hooks, a generated-project workflow, and final regression scripts.

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
- `generator`: TinyRPC generated-project CLI and templates.
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

## Regression Scripts

| Script | Purpose |
|---|---|
| `scripts/check_rpc_sync.sh` | Synchronous TinyPB RPC safety net. |
| `scripts/check_rpc_async.sh` | Asynchronous TinyPB RPC lifecycle and timeout/cancel regression. |
| `scripts/check_stage11_server.sh` | Multi-Reactor TinyPB server regression. |
| `scripts/check_stage12_http.sh` | HTTP server regression. |
| `scripts/check_generator.sh` | Generator template and service/method skeleton regression. |
| `scripts/check_generator_project.sh` | Generated project build/start/client/shutdown regression. |
| `scripts/check_all.sh` | Full local regression across the project. |

## Minimal Runtime Entry

TinyPB server:

```cpp
#include "comm/start.h"

int main()
{
    tinyrpc::InitConfig("conf/test_tinypb_server.xml");
    tinyrpc::StartRpcServer();
    REGISTER_SERVICE(QueryServiceImpl);
    tinyrpc::GetServer()->start();
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
    tinyrpc::GetServer()->start();
}
```

`StartRpcServer()` creates and initializes the server from XML. `GetServer()->start()` enters the blocking event loop after services or servlets have been registered.

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

## Current Boundaries

- This is a learning implementation, not a production RPC distribution.
- HTTP is a minimal request/response server path; HTTPS, HTTP/2, chunked, and streaming are out of scope.
- The generated project depends on the local MyTinyRPC source tree through `MYTINYRPC_ROOT`.
- `TcpServer::start()` is blocking; script-driven examples stop servers by process management.
- Connection pools, load balancing, MySQL plugins, full tracing, and performance reports are intentionally not part of the current scope.

## Editor Note

When using a C++ language server, open the repository in the same Linux/WSL environment used for builds. If `build/compile_commands.json` contains paths from another environment, remove `build/` and rerun:

```bash
rm -rf build
bash build.sh
```
