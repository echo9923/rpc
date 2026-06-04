# 删除 TcpServer::waitOnce 并清理相关测试调用

## 变更清单

### 1. `mytinyrpc/net/tcpserver.h`

- 删除 `int waitOnce(int timeoutMs);` 声明（line 42）
- 新增 `Reactor* getReactor() { return &m_reactor; }` 公有访问器，供测试直接驱动事件循环

### 2. `mytinyrpc/net/tcpserver.cc`

- 删除 `TcpServer::waitOnce` 函数定义（line 122-125）

### 3. `testcases/test_start.cc`

- 第 107 行 `server->waitOnce(10)` → `server->getReactor()->waitOnce(10)`
- `AddTimerTaskRunsOnServerReactor` 测试无需 pump reactor 来处理定时器，改为通过 Reactor 指针直接调用

## 影响分析

- `TcpServer::start()` 内部直接调用 `m_reactor.waitOnce(-1)`，不受影响
- 仅有 `testcases/test_start.cc` 一处外部调用 `TcpServer::waitOnce`，已纳入清理
- 其他文件中的 `waitOnce` 调用均为 `Reactor::waitOnce`，不在本次修改范围内
