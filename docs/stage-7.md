# 阶段 7：客户端连接骨架

## 目标

阶段 7 在阶段 6（RPC 服务注册与分发）的基础上，引入最小同步 `TcpClient`，只负责客户端 TCP 连接生命周期：保存对端地址、创建 socket、阻塞式 `connect()`、记录错误、关闭 socket、析构时安全释放资源。为后续 TinyPB 客户端收发和 `TinyPbRpcChannel` 提供连接基础。

## 当前进度

| 任务 | 状态 | 说明 |
|------|------|------|
| 任务三十六：实现最小 TcpClient 连接骨架 | 已完成 | 同步阻塞式 connect/close，记录错误，析构安全释放。 |

## 任务三十六记录

任务三十六完成的目标是实现一个最小同步 `TcpClient`，只做连接管理，不涉及任何协议编解码或 RPC 逻辑。

- 新增 `mytinyrpc/net/tcpclient.h`：
  - `TcpClient` 类声明，`namespace tinyrpc`。
  - 构造函数接收 `const IPAddress& peerAddr`，保存对端地址。
  - 析构函数自动调用 `closeConnection()` 释放 socket。
  - 禁止拷贝（`= delete`）。
  - 公共方法：`getPeerAddress()`、`getFd()`、`isConnected()`、`getErrorInfo()`、`connectServer()`、`closeConnection()`。
  - 成员变量：`m_peerAddr`（IPAddress）、`m_fd`（Socket）、`m_isConnected`（bool）、`m_errorCode`（int）。
- 新增 `mytinyrpc/net/tcpclient.cc`：
  - `connectServer()`：
    1. 若已连接直接返回 `true`。
    2. `socket(AF_INET, SOCK_STREAM, 0)` 创建 TCP socket。
    3. `connect(m_fd, ...)` 阻塞式连接对端。
    4. 成功：`m_isConnected = true`、`m_errorCode = 0`。
    5. 失败：保存 `errno` 到 `m_errorCode`，关闭 socket，返回 `false`。
  - `closeConnection()`：关闭 fd（幂等，检查 `m_fd != kInvalidSocket`），重置状态。
  - `getErrorInfo()`：`m_errorCode == 0` 返回空字符串，否则返回 `strerror(m_errorCode)`。
- 新增 `testcases/test_tcp_client.cc`（GTest）：
  - `ConstructorSavesPeerAddress`：验证 `getPeerAddress()` 返回构造时的地址。
  - `InitiallyNotConnected`：初始状态 `isConnected() == false`、`getFd() == kInvalidSocket`、`getErrorInfo().empty()`。
  - `ConnectToNonListeningPort`：连接不存在的端口，验证返回 `false` 且 `getErrorInfo()` 非空。
  - `ConnectToListeningPort`：在测试 fixture 中创建临时监听 socket，验证 `connectServer()` 成功、`isConnected() == true`、重复调用幂等。
  - `CloseThenNotConnected`：`closeConnection()` 后状态正确重置，多次调用安全。
  - `DestructorClosesConnection`：验证析构函数自动关闭连接。
- 修改 `CMakeLists.txt`：
  - `SRC` 列表新增 `mytinyrpc/net/tcpclient.cc`。
  - 新增 `test_tcp_client` 编译目标（GTest）。

## 构建

```bash
./build.sh
```

## 运行

```bash
./build/test_tcp_client
```

## 回归测试

```bash
./build/test_tinypb_dispatcher
./build/test_connection_codec
./build/test_protobuf_service
./scripts/check_stage1.sh
```

## 已知限制

1. `connectServer()` 使用阻塞式 socket，在高并发场景下会阻塞调用线程。
2. 不支持非阻塞连接、超时、重试、连接池。
3. 不支持异步回调、协程 hook 或 Reactor 集成。
4. 不包含任何协议编解码能力（TinyPB 等）。

## 下一阶段

在 `TcpClient` 连接骨架上逐步添加：
1. TinyPB 请求发送与响应读取。
2. 最小 `TinyPbRpcChannel` 实现。
3. 超时与错误处理增强。
