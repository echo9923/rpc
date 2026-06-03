# 阶段 7：客户端连接与最小 TinyPB 同步收发

## 目标

阶段 7 在阶段 6（RPC 服务注册与分发）的基础上，引入最小同步 `TcpClient`。当前客户端已经支持 TCP 连接生命周期管理，并能复用 `TinyPbCodec` 与 `TcpBuffer` 完成最小 TinyPB 请求发送、响应读取和同步请求/响应闭环。该阶段为后续 `TinyPbRpcChannel` 和 Protobuf Stub 调用提供客户端侧协议收发基础。

## 当前进度

| 任务 | 状态 | 说明 |
|------|------|------|
| 任务三十六：实现最小 TcpClient 连接骨架 | 已完成 | 同步阻塞式 connect/close，记录错误，析构安全释放。 |
| 任务三十七：让 TcpClient 完成最小 TinyPB 同步请求/响应闭环 | 已完成 | 同步发送 TinyPB 请求帧，读取并解码 TinyPB 响应帧，提供 `sendAndRecvTinyPb()`。 |

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

## 任务三十七记录

任务三十七在任务三十六连接骨架上增加最小 TinyPB 同步收发能力，只打通 `TinyPbStruct` 到 socket 字节流再回到 `TinyPbStruct` 的小闭环，不接入 `RpcChannel`、Protobuf Stub、请求号生成、超时、重试、连接池、异步回调、协程 hook 或 Reactor 客户端化。

- 修改 `mytinyrpc/net/tcpclient.h`：
  - 引入 `TcpBuffer`、`TinyPbCodec` 和 `TinyPbStruct`。
  - 新增公共方法：`sendTinyPbRequest()`、`recvTinyPbResponse()`、`sendAndRecvTinyPb()`。
  - 新增私有辅助方法：`writeAll()`、`readSomeToBuffer()`。
  - 新增 `m_errorInfo`，`getErrorInfo()` 优先返回自定义错误信息，否则返回 `m_errorCode` 对应的 `strerror()`。
- 修改 `mytinyrpc/net/tcpclient.cc`：
  - `sendTinyPbRequest()`：参数校验后自动确保连接，使用 `TinyPbCodec` 编码请求，再通过 `writeAll()` 循环写完整帧。
  - `recvTinyPbResponse()`：要求已连接，循环 `read()` 追加到局部 `TcpBuffer`，反复调用 `TinyPbCodec::decode()`，直到拿到完整响应或遇到 EOF/错误。
  - `sendAndRecvTinyPb()`：串联发送和接收，两步都成功才返回 `true`。
  - `writeAll()` 和 `readSomeToBuffer()` 处理 `EINTR`，暂不实现 `EAGAIN/EWOULDBLOCK` 等复杂等待。
- 扩展 `testcases/test_tcp_client.cc`：
  - 保留任务三十六已有连接生命周期测试。
  - 新增 `SendTinyPbRequestWritesFrame`，验证客户端写出的请求帧可被服务端侧 `TinyPbCodec` 解码。
  - 新增 `RecvTinyPbResponseDecodesFrame`，验证客户端可读取并解码服务端写回的响应帧。
  - 新增 `SendAndRecvTinyPbRoundTrip`，验证一次同步 TinyPB 请求/响应 round trip。
  - 新增 `SendTinyPbRequestRejectsInvalidRequest`，验证缺少必要字段的请求会失败且不写出非法帧。

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
./build/test_tinypb_codec
./build/test_tinypb_dispatcher
./build/test_connection_codec
./build/test_protobuf_service
./scripts/check_stage1.sh
```

## 已知限制

1. `connectServer()` 使用阻塞式 socket，在高并发场景下会阻塞调用线程。
2. TinyPB 收发也是同步阻塞式，只处理 `EINTR`，不实现 `EAGAIN/EWOULDBLOCK` 等复杂等待。
3. 不支持非阻塞连接、超时、重试、连接池。
4. 不支持异步回调、协程 hook 或 Reactor 集成。
5. 不实现 `google::protobuf::RpcChannel`，不接入 Protobuf Stub，不根据 `MethodDescriptor` 自动填充 `serviceFullName`。
6. 不生成请求号、不做请求号匹配表；调用方仍需自行填写 `TinyPbStruct::m_reqId` 和 `m_serviceFullName`。

## 下一阶段

在当前 `TcpClient` TinyPB 同步收发能力上继续添加：

1. 最小 `TinyPbRpcChannel` 实现。
2. 端到端 RPC 示例与验收脚本。
3. 请求号生成、超时与错误处理增强。
