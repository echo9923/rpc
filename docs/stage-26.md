# 阶段 26：TcpServer 优雅停止和生命周期收口

阶段 26 的目标是把阶段 25 标出的 `TcpServer::stop()` 边界补齐，让服务端具备框架层停止入口，而不是只能依赖脚本杀进程。

## 任务一百二十四：增加 `TcpServer` 优雅停止入口

已完成能力：

- `TcpServer` 新增 `stop()`，可从其他线程唤醒阻塞在 `start()` 内的主 Reactor。
- `TcpServer` 新增 `isRunning()`，用于测试和上层代码观察事件循环状态。
- `TcpServer::start()` 退出后统一执行 shutdown 路径，清理监听 fd、连接表和 IOThreadPool。
- 析构函数复用 `stop()` / shutdown 路径，避免析构和显式停止维护两套关闭逻辑。
- `comm/start` 新增 `StopRpcServer()`，供启动门面和生成工程信号处理统一调用。
- 生成 server 模板安装 `SIGTERM` / `SIGINT` handler，收到普通 kill 或 Ctrl-C 时调用框架层停止入口。

验证命令：

```bash
cmake --build build --target test_tinypb_server_client test_http_server
```

## 任务一百二十五：新增 TcpServer 生命周期验收

已完成能力：

- 新增 `test_tcpserver_lifecycle`，覆盖单 Reactor `stop()` 唤醒、活跃连接清理、多 Reactor 连接清理、重复 stop 和端口释放。
- 新增 `scripts/check_stage26_lifecycle.sh`，作为阶段 26 专项验收入口。
- `scripts/check_all.sh` 接入阶段 26 生命周期回归。

验证命令：

```bash
MYTINYRPC_SKIP_BUILD=1 ./scripts/check_stage26_lifecycle.sh
```

已验证结果：

- 2026-06-11 在 WSL 中通过，最终输出 `[stage26-lifecycle] PASS`。

## 任务一百二十六：修复连接协程生命周期保活

已完成能力：

- `TcpConnection` 的读协程回调从捕获 `shared_ptr` 改为捕获 `weak_ptr`，避免连接对象通过自身协程回调形成引用环。
- `closeWithCallback()` 在连接协程内部触发时，将关闭回调延后投递到所属 Reactor task，保证协程返回前连接对象仍由服务端连接表保活。
- `TcpConnection` 增加测试观察用存活计数，生命周期测试可验证对端关闭后连接对象不被协程回调长期保活。
- `test_tcpserver_lifecycle` 新增 `ClosedPeerDoesNotKeepServerConnectionAlive`，覆盖连接表清空和对象析构。

验证命令：

```bash
./build/test_tcpserver_lifecycle
```

## 当前边界

- `TcpServer::start()` 仍是阻塞式事件循环；需要异步启动时由调用方放入独立线程。
- `stop()` 负责停止当前服务端和清理已接入的连接/线程资源，不提供同一 `TcpServer` 对象上的 restart 语义。
- HTTP keep-alive、服务发现、连接池和 OpenTelemetry 仍属于后续独立阶段；发布级生成工程源码包已由阶段 31 补齐。
