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

Install WSL dependencies if needed:

```bash
sudo apt update
sudo apt install -y build-essential cmake netcat-openbsd
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

## Current Limitations

The current server uses a single-threaded blocking model.

While one client connection is open, the server cannot accept the next client.
This is an intentional Stage 1 limitation. Stage 2 will introduce non-blocking
sockets and `epoll` as preparation for Reactor.
