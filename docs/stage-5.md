# 阶段 5：连接层协议接入

## 目标

阶段 5 在阶段 4（协议编解码接口准备）的基础上将 `TcpConnection` 的主循环重构为 `input → execute → output` 三段式，为后续接入 `TinyPbCodec` 等具体协议提供清晰的插入点。

## 当前进度

| 任务 | 状态 | 说明 |
|------|------|------|
| 任务三十一：把 TcpConnection 重构为 input / execute / output 三段式主循环 | 已完成 | `coroutineReadLoop` 拆分为 `input()` / `execute()` / `output()`，外部行为不变。 |
| 任务三十二：为 TcpConnection 接入 AbstractCodec，并实现 TinyPB 最小协议回环 | 已完成 | `execute()` 根据 `m_codec` 是否为空走 Echo 或 TinyPB 回环逻辑。 |

## 任务三十一记录

任务三十一完成的目标是将 `TcpConnection::coroutineReadLoop()` 从单体循环拆解为 `input() → execute() → output()` 三段式结构，为后续接入协议编解码层做好准备：

- 修改 `mytinyrpc/net/tcpconnection.h`：
  - 新增 `bool input()`、`void execute()`、`void output()` 私有方法。
  - 删除 `void flushOutputByHook()` 声明（已重命名为 `output`）。
- 修改 `mytinyrpc/net/tcpconnection.cc`：
  - `coroutineReadLoop()` 简化为三段式驱动循环：`while (!m_isClosed) { if (!input()) break; execute(); output(); }`。
  - `input()`：从 socket 读取数据并追加到 `m_inputBuffer`；在方法内部处理 EINTR 重试；连接关闭时调用 `closeWithCallback()` 并返回 `false`。
  - `execute()`：消费 `m_inputBuffer`，当前保持 Echo 语义（`retrieveAllAsString()` → 追加到 `m_outputBuffer`）。
  - `output()`：原名 `flushOutputByHook()`，功能不变，通过 `write_hook` 将 `m_outputBuffer` 刷到 socket。
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
