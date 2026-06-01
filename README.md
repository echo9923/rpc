# MyTinyRPC

MyTinyRPC is a TinyRPC learning project.

## Stage 1: Blocking TCP Echo Server

Stage 1 implements a minimal blocking TCP Echo Server.

### Implemented

- CMake build
- Console logger
- IPv4 address wrapper
- TCP server
- TCP connection
- Blocking `accept`
- Blocking `read` / `write`
- Multiple echo messages on one connection
- Stage 1 acceptance script

### Not Implemented

- Non-blocking sockets
- `epoll`
- Reactor
- Concurrent multi-client handling
- HTTP
- TinyPB
- Protobuf
- RPC calls
- Coroutines

## Stage 2: Non-blocking IO Preparation

Stage 2 is now in progress. The current completed step is task 8:

- added fd utility helpers
- set the listen fd to non-blocking mode
- handled `accept()` returning `EAGAIN` / `EWOULDBLOCK`
- kept client fds blocking for now
- kept `epoll` and Reactor out of scope for this step

See [阶段 2：非阻塞 IO 与 Reactor 准备](docs/stage-2.md).

## Build

Windows PowerShell:

```powershell
.\build.ps1
```

Linux/WSL:

```bash
./build.sh
```

## Sync RPC Check

Stage 9 has a stable synchronous TinyPB RPC regression path:

```text
QueryService_Stub -> TinyPbRpcChannel -> TcpClient -> TinyPB -> TcpServer -> TinyPbDispatcher -> QueryServiceImpl -> response
```

Run the synchronous RPC regression in Linux/WSL:

```bash
./scripts/check_rpc_sync.sh
```

Expected final output:

```text
[rpc-sync] PASS
```

The lighter Stage 8 script `./scripts/check_rpc_sync_basic.sh` remains available for the minimal path. After Stage 9, later stages should run `./scripts/check_rpc_sync.sh` before closing the task to catch sync RPC regressions.

Current sync RPC limitations: no async RPC, no connection pool, no response multiplexing, and no out-of-order response cache.

## Minimal RPC Server Entry

Stage 13 adds a shorter startup entry for XML-driven servers:

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

For HTTP:

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

Install WSL dependencies if needed:

```bash
sudo apt update
sudo apt install -y build-essential cmake netcat-openbsd libgtest-dev protobuf-compiler libprotobuf-dev
```

## Run

```bash
./build/test_tcp_echo_server
```

The server listens on `127.0.0.1:19999`.

## Manual Test

```bash
nc 127.0.0.1 19999
```

Input:

```text
hello
```

Expected response:

```text
hello
```

## Stage 1 Check

```bash
./scripts/check_stage1.sh
```

Expected final output:

```text
[stage1] PASS
```

The script builds the project, starts the echo server, sends test messages,
checks the echoed responses, prints the server log on failure, and shuts the
server process down before exiting.

The script depends on `nc`. Install `netcat-openbsd` or another compatible
netcat package if `nc` is missing.

## Windsurf C++ 跳转

如果在 Windsurf 里 `F12`、`Ctrl+Click` 或 `Go to Definition` 失效，优先按下面的顺序排查：

1. 使用 **WSL 窗口** 打开仓库，不要继续在 Windows 本地窗口里复用已有的 `build/`。
2. 切换到 Linux/WSL 环境后，先清理旧的构建目录，再重新生成编译数据库：

```bash
cd /mnt/d/codeproject/cpp/rpc
rm -rf build
bash build.sh
```

3. 仓库已提供下列工作区配置，供 `clangd` 和 `CMake Tools` 复用同一个 `build/` 目录：
   - `.clangd` 固定从 `build/` 读取 compilation database。
   - `.vscode/settings.json` 固定 `cmake.buildDirectory` 为 `${workspaceFolder}/build`，并开启 `CMAKE_EXPORT_COMPILE_COMMANDS`。
   - `.vscode/extensions.json` 推荐安装 `Windsurf C++ Tools`、`clangd` 和 `CMake Tools`。
4. 重建完成后先验证 `F12`。如果 `F12` 正常但 `Ctrl+Click` 不跳转，继续检查 Windsurf/VS Code 的 `editor.multiCursorModifier` 设置；这类情况下通常会变成 `Alt+Click` 跳转定义。

如果重新生成后的 `build/compile_commands.json` 仍然是 Windows 路径或 Docker 路径，说明当前工作区仍然不是你实际使用的 C++ 语言服务器环境，需要先统一打开方式再继续排查。

## Current Limitations

The current server uses a single-threaded blocking model.

While one client connection is open, the server cannot accept the next client.
This is an intentional Stage 1 limitation. Stage 2 will introduce non-blocking
sockets and `epoll` as preparation for Reactor.
