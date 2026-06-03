# 阶段 5：连接层协议接入

## 目标

阶段 5 在阶段 4（协议编解码接口准备）的基础上将 `TcpConnection` 的主循环重构为 `input → execute → output` 三段式，为后续接入 `TinyPbCodec` 等具体协议提供清晰的插入点。

## 当前进度

| 任务 | 状态 | 说明 |
|------|------|------|
| 任务三十一：把 TcpConnection 重构为 input / execute / output 三段式主循环 | 已完成 | `coroutineReadLoop` 拆分为 `input()` / `execute()` / `output()`，外部行为不变。 |
| 任务三十二：为 TcpConnection 接入 AbstractCodec，并实现 TinyPB 最小协议回环 | 已完成 | `execute()` 根据 `m_codec` 是否为空走 Echo 或 TinyPB 回环逻辑。 |
| 任务三十三：引入 AbstractDispatcher，并让 TcpConnection 通过 TinyPB Dispatcher 生成响应 | 已完成 | `execute()` 解码后交给 dispatcher 处理，由 dispatcher 通过 `sendProtocolData()` 写回响应。 |

## 任务三十一记录

任务三十一完成的目标是将 `TcpConnection::coroutineReadLoop()` 从单体循环拆解为 `input() → execute() → output()` 三段式结构，为后续接入协议编解码层做好准备：

- 修改 `mytinyrpc/net/tcpconnection.h`：
  - 新增 `bool input()`、`void execute()`、`void output()` 私有方法。
  - 删除 `void flushOutputByHook()` 声明（已重命名为 `output`）。
- 修改 `mytinyrpc/net/tcpconnection.cc`：
  - `coroutineReadLoop()` 简化为三段式驱动循环：`while (!m_isClosed) { if (!input()) break; execute(); output(); }`。
  - `input()`：从 socket 读取数据并追加到 `m_inputBuffer`；在方法内部处理 EINTR 重试；连接关闭时调用 `closeWithCallback()` 并返回 `false`。
  - `execute()`：消费 `m_inputBuffer`，当前保持 Echo 语义（`retrieveAllAsString()` → 追加到 `m_outputBuffer`）。
  - `output()`：原名 `flushOutputByHook()`，功能不变，通过 `writeHook` 将 `m_outputBuffer` 刷到 socket。
- 当前 Echo Server 外部行为不变。

## 任务三十二记录

任务三十二完成的目标是将连接层和协议层第一次接上，实现 TinyPB 最小协议回环（decode 一帧 → encode 回一帧），同时保持 `codec == nullptr` 时的 Echo 行为不变。

- 修改 `mytinyrpc/net/tcpserver.h`：
  - 构造函数新增 `AbstractCodec::Ptr codec = nullptr` 参数。
  - 新增 `AbstractCodec::Ptr m_codec` 成员。
- 修改 `mytinyrpc/net/tcpserver.cc`：
  - 构造函数初始化 `m_codec`。
  - `acceptLoop()` 创建 `TcpConnection` 时将 `m_codec` 传入。
- 修改 `mytinyrpc/net/tcpconnection.h`：
  - 构造函数新增 `AbstractCodec::Ptr codec = nullptr` 参数。
  - 新增 `AbstractCodec::Ptr m_codec` 成员。
  - 新增 `FRIEND_TEST` 声明，允许测试访问私有成员。
- 修改 `mytinyrpc/net/tcpconnection.cc`：
  - 构造函数初始化 `m_codec`。
  - `execute()` 重写为双路径：
    - `m_codec == nullptr`：保持 Echo 语义（`retrieveAllAsString` → `append`）。
    - `m_codec != nullptr`：循环 `decode` → `encode`，处理粘包；半包时不消费 buffer，等下一轮 `input()`。
- 新增 `testcases/test_connection_codec.cc`：
  - `EchoFallbackNoCodec`：不配 codec 时 `execute()` 走 Echo 语义。
  - `TinyPbRoundTrip`：配 TinyPbCodec，encode 一帧到 inputBuffer，execute 后 outputBuffer 中 decode 出的字段与输入一致。
  - `TinyPbPartialFrameNoConsume`：半包时 execute 不消费 inputBuffer，outputBuffer 为空。
- 修改 `CMakeLists.txt`：新增 `test_connection_codec` 编译目标。

## 任务三十三记录

任务三十三完成的目标是将业务处理逻辑从 `TcpConnection` 移出到 `AbstractDispatcher`，实现 `decode → dispatcher->dispatch() → sendProtocolData() → outputBuffer` 的链路。

- 新增 `mytinyrpc/net/abstractdispatcher.h`：
  - 定义 `AbstractDispatcher` 抽象基类，提供 `dispatch(AbstractData*, TcpConnection*)` 纯虚方法。
- 新增 `mytinyrpc/net/tinypb/tinypbdispatcher.h` + `tinypbdispatcher.cc`：
  - `TinyPbDispatcher` 继承 `AbstractDispatcher`，实现最小分发逻辑。
  - `dispatch()`：`dynamic_cast` 请求为 `TinyPbStruct`，解析 `serviceFullName`，构造响应（保留 `reqId`、`serviceFullName`、`pbData`），调用 `conn->sendProtocolData()` 写回。
  - `parseServiceFullName()`：以 `.` 分割服务名和方法名，任一为空则返回 false。
- 修改 `mytinyrpc/net/tcpconnection.h`：
  - 构造函数新增 `AbstractDispatcher::Ptr dispatcher = nullptr` 参数。
  - 新增 `AbstractDispatcher::Ptr m_dispatcher` 成员。
  - 新增公共方法：`getCodec()`、`getOutputBuffer()`、`sendProtocolData()`。
- 修改 `mytinyrpc/net/tcpconnection.cc`：
  - 构造函数初始化 `m_dispatcher`。
  - `execute()` 中 codec 路径改为：decode 成功后，有 dispatcher 则调用 `m_dispatcher->dispatch(&pb, this)`；无 dispatcher 则 `encode` 回环（向后兼容）。
  - 新增 `sendProtocolData()`：调用 `m_codec->encode()` 将协议数据编码后写入 `m_outputBuffer`。
- 修改 `mytinyrpc/net/tcpserver.h`：
  - 构造函数新增 `AbstractDispatcher::Ptr dispatcher = nullptr` 参数。
  - 新增 `AbstractDispatcher::Ptr m_dispatcher` 成员。
- 修改 `mytinyrpc/net/tcpserver.cc`：
  - 构造函数初始化 `m_dispatcher`。
  - `acceptLoop()` 创建 `TcpConnection` 时将 `m_dispatcher` 传入。
- 新增 `testcases/test_tinypb_dispatcher.cc`：
  - `ParseServiceFullNameValid`：合法 `serviceFullName` 正确拆分。
  - `ParseServiceFullNameRejectsInvalid`：空字符串、无 `.`、`.method`、`Service.` 均返回 false。
  - `DispatchWritesTinyPbResponse`：dispatch 后 `outputBuffer` 中可解码出正确的响应。
- 更新 `testcases/test_connection_codec.cc`：
  - 新增 `SendProtocolDataWritesOutput`：验证 `sendProtocolData()` 将协议数据编码后写入 `outputBuffer`。
- 修改 `CMakeLists.txt`：
  - SRC 列表新增 `tinypbdispatcher.cc`。
  - 新增 `test_tinypb_dispatcher` 编译目标。
