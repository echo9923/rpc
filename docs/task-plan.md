# TinyRPC 渐进式复刻任务计划书（重构版）

> **适用项目**：`https://github.com/echo9923/rpc.git`  
> **参考项目**：`https://github.com/Gooddbird/tinyrpc`  
> **当前基线**：已完成到任务三十七，`TcpClient` 已具备最小 TinyPB 同步请求/响应闭环。  
> **规划目标**：从当前能力继续推进，按“同步 RPC → Reactor 增强 → 多线程 → HTTP → 配置日志 → 协程 → 异步 RPC → 生成器 → 文档收口”的顺序，渐进式复刻 TinyRPC 风格 C++ RPC 框架。  
> **核心原则**：每个任务都必须可独立理解、可独立实现、可独立验证、可独立回退。

---

## 0. 规划原则

1. **先主链路，后工程化**：先跑通 TinyPB 同步 RPC，再补超时、错误码、配置、日志、生成器。
2. **先单线程，后多线程**：先把单 Reactor、Timer、wakeup、连接生命周期吃透，再接入 IOThreadPool。
3. **先同步，后异步**：异步 RPC 必须建立在 reqId、Timer、Reactor task queue、IOThread 和连接生命周期稳定之后。
4. **先协议本身，后服务集成**：HTTP 先做 request/response/codec，再接入 TcpServer。
5. **先最小行为，后完整复刻**：复杂模块先做最小可理解版本，再决定是否继续追求原项目复杂度。
6. **每阶段必须收口**：每个阶段结束时补阶段文档、调用链图、回归脚本和当前限制说明。
7. **不机械复制原项目**：保留 TinyRPC 的核心思想，但允许按自己的命名、目录和学习节奏实现。

---

## 1. 当前状态摘要

### 1.1 已完成能力

- 最小构建系统、README、阶段文档和基础验收脚本。
- 阻塞式 TCP Echo Server。
- 非阻塞 fd、`epoll` demo、`FdEvent`、`Reactor`。
- `TcpBuffer`。
- `TcpConnection` 输入、执行、输出三段式流程。
- 最小协程对象、`readHook/writeHook` 和服务端读写路径协程化。
- `AbstractData`、`AbstractCodec`、`AbstractDispatcher`。
- TinyPB 数据结构、编码、解码、流式拆包、错误包恢复。
- Protobuf 服务注册、方法查找、服务端 RPC 分发闭环。
- `TcpClient` 连接生命周期和最小 TinyPB 同步收发。

### 1.2 当前主要缺口

- 缺少 `google::protobuf::RpcChannel` 客户端封装。
- 缺少真实客户端 Stub 到服务端的端到端网络 RPC 验收。
- 客户端请求号、超时、错误码、重试和连接语义仍很薄。
- Reactor 缺少完整 Timer、timerfd、跨线程 task queue、wakeup 和 stop 语义。
- 服务端尚未形成 Main Reactor + Sub Reactor 的多 IO 线程模型。
- HTTP 协议栈尚未复刻。
- 配置、日志、启动入口、服务注册宏和运行时上下文尚未复刻。
- 协程 hook、协程池、内存池尚未系统化。
- 异步 RPC Channel、代码生成器、最终示例和完整文档尚未完成。

---

## 2. 总体阶段路线

| 阶段 | 名称 | 核心目标 | 类型 |
|---|---|---|---|
| 阶段八 | 同步 RPC 客户端闭环 | Stub 能通过 `TinyPbRpcChannel` 调用真实服务端 | 必须复刻 |
| 阶段九 | 同步客户端连接语义收口 | 请求号、错误码、超时、重连边界清楚 | 必须复刻 / 简化复刻 |
| 阶段十 | Timer、Reactor wakeup 和连接生命周期 | Reactor 支持时间事件、跨线程任务和安全退出 | 必须复刻 |
| 阶段十一 | IOThread 与服务端多 Reactor | Main Reactor accept，Sub Reactor 处理连接读写 | 必须复刻 |
| 阶段十二 | HTTP 协议栈 | HTTP request/response/codec/dispatcher/server 闭环 | 必须复刻 / 简化复刻 |
| 阶段十三 | 配置、日志、启动入口和运行时 | 从测试程序升级为框架化启动方式 | 必须复刻 / 简化复刻 |
| 阶段十四 | 协程、hook、协程池和内存池整理 | 理解 hook 与 Reactor 的关系，整理协程能力 | 简化复刻 |
| 阶段十五 | 异步 RPC Channel | 支持并发异步调用、超时、回调和 reqId 匹配 | 必须复刻 |
| 阶段十六 | 代码生成器与示例工程 | 生成业务工程骨架并可端到端运行 | 简化复刻 |
| 阶段十七 | 工程收口、覆盖矩阵和最终文档 | 全量回归、示例、学习总结、原项目对照 | 必须复刻 |

---

## 3. 功能复刻边界

### 3.1 必须复刻

- TinyPB 编解码。
- Protobuf 服务注册与服务端分发。
- `TcpClient` 同步 TinyPB 请求/响应。
- `TinyPbRpcChannel`。
- `TinyPbRpcController` 的基本错误语义。
- 请求号 `reqId`。
- 同步 RPC 端到端示例。
- Timer / timerfd。
- Reactor task queue / wakeup / stop。
- IOThread / IOThreadPool。
- TcpServer 多 Reactor。
- HTTP codec / servlet / dispatcher / server 示例。
- 配置、日志、启动入口的最小可用版本。
- 异步 RPC Channel 的核心生命周期。
- 全量回归脚本和覆盖矩阵。

### 3.2 简化复刻

- 同步客户端超时：可先用 `poll()` 或 `SO_RCVTIMEO/SO_SNDTIMEO`，不急于客户端 Reactor 化。
- 客户端重连：只做有限重试和显式 close 后重连，不做连接池。
- HTTP：只支持常见 GET/POST、header、Content-Length，不做 HTTPS、HTTP/2、chunked。
- 配置：只读取核心字段，不复刻所有插件配置。
- 日志：先同步文件日志，再做简化异步 flush，不强制复杂滚动策略。
- 协程 hook：重点做 `connect`、`sleep/usleep`，`recv/send/accept` 可以简化。
- 生成器：先模板复制，再支持简单 proto service 解析，不追求完整 proto parser。

### 3.3 暂不复刻或最后再考虑

- MySQL 插件。
- 完整复杂内存池。
- 高级连接池。
- 高性能压测优化。
- HTTPS / HTTP/2。
- 完整 tracing 系统。
- 与原项目 100% 行为一致。

---

# 阶段八：同步 RPC 客户端闭环

## 阶段目标

将当前“手写 `TinyPbStruct` 收发”的能力升级为“Protobuf 生成的 Stub 可以通过网络调用服务端”。

## 阶段完成标准

- `TinyPbRpcChannel` 可被 Protobuf Stub 使用。
- Stub 发出的 request 能被真实服务端 dispatcher 分发到真实 service。
- response 能从服务端返回并反序列化到业务 response。
- 至少有一个端到端同步 RPC 脚本可一键验收。
- 文档能画出：Stub -> RpcChannel -> TcpClient -> TinyPB -> TcpServer -> Dispatcher -> Service -> Response。

---

## 任务三十八：实现最小 `TinyPbRpcChannel`

**类型**：必须复刻

### 学习目标

理解 `google::protobuf::RpcChannel` 的职责：它是 Protobuf Stub 和网络传输层之间的适配器。Stub 不知道 TCP，也不知道 TinyPB；`TinyPbRpcChannel` 负责把 `CallMethod()` 里的 request 转成 TinyPB 请求，并把 TinyPB 响应转回 Protobuf response。

### 实现目标

- 新增 `TinyPbRpcChannel`，继承 `google::protobuf::RpcChannel`。
- 在 `CallMethod()` 中读取 `MethodDescriptor::full_name()`。
- 将 Protobuf request 序列化到 `TinyPbStruct::m_pbData`。
- 调用 `TcpClient::sendAndRecvTinyPb()` 完成同步收发。
- 将 response 的 `m_pbData` 反序列化到 Protobuf response。
- 使用 `TinyPbRpcController` 记录框架层错误。

### 关键文件

- `mytinyrpc/net/tinypb/tinypbrpcchannel.h`
- `mytinyrpc/net/tinypb/tinypbrpcchannel.cc`
- `mytinyrpc/net/tinypb/tinypbrpccontroller.h`
- `mytinyrpc/net/tinypb/tinypbrpccontroller.cc`
- `mytinyrpc/net/tcpclient.h`
- `mytinyrpc/net/tcpclient.cc`
- `testcases/test_tinypb_rpc_channel.cc`
- `CMakeLists.txt`
- `docs/stage-8.md`
- `docs/replica-progress.md`

### 建议最小接口

```cpp
class TinyPbRpcChannel : public google::protobuf::RpcChannel {
public:
  explicit TinyPbRpcChannel(const IPAddress& peerAddr);

  void CallMethod(
      const google::protobuf::MethodDescriptor* method,
      google::protobuf::RpcController* controller,
      const google::protobuf::Message* request,
      google::protobuf::Message* response,
      google::protobuf::Closure* done) override;

  void setReqIdGenerator(std::function<std::string()> generator); // 可选，仅测试用
};
```

### 测试方式

- 写 `test_tinypb_rpc_channel.cc`。
- 使用本地 mock socket server，不启动真实 `TcpServer`。
- 测试合法 request / response。
- 测试服务端返回 TinyPB 错误码。
- 测试 response `pbData` 非法导致反序列化失败。
- 测试 `done` closure 是否执行。

### 验收标准

- Protobuf Stub 能调用 `TinyPbRpcChannel::CallMethod()`。
- mock server 能解出正确 `serviceFullName` 和 request `pbData`。
- 客户端能解析合法 response。
- controller 能表达序列化失败、网络失败、服务端错误、反序列化失败。
- 既有 TinyPB codec、dispatcher、TcpClient 测试不回退。

### 不包括

- 不启动真实 `TcpServer`。
- 不做真实 Stub 到服务端端到端测试。
- 不做超时。
- 不做重试。
- 不做连接池。
- 不做异步调用。

---

## 任务三十九：真实 Stub 到服务端端到端同步 RPC

**类型**：必须复刻

### 学习目标

理解完整同步 RPC 调用链，而不是只验证局部模块。

### 实现目标

- 新增真实服务端测试程序，注册 `QueryServiceImpl`。
- 新增客户端测试程序，使用 Protobuf Stub + `TinyPbRpcChannel` 发起调用。
- 验证链路：Stub -> Channel -> TcpClient -> TinyPB -> TcpServer -> Dispatcher -> Service -> Response。

### 关键文件

- `testcases/test_tinypb_server_client.cc`
- `testcases/test_tinypb_server.proto`
- `mytinyrpc/net/tcpserver.h`
- `mytinyrpc/net/tcpserver.cc`
- `mytinyrpc/net/tinypb/tinypbdispatcher.h`
- `mytinyrpc/net/tinypb/tinypbdispatcher.cc`
- `scripts/check_stage8_rpc.sh`
- `docs/stage-8.md`

### 测试方式

- 脚本启动服务端。
- 等待端口可连接。
- 运行 Stub 客户端。
- 客户端发起一次真实 RPC。
- 脚本清理服务端进程。

### 验收标准

- `./scripts/check_stage8_rpc.sh` 输出 `[stage8] PASS`。
- 请求能进入真实 `QueryServiceImpl`。
- response 字段符合预期。
- `./scripts/check_stage1.sh` 仍通过。

### 不包括

- 不做多客户端并发。
- 不做超时重试。
- 不做异步 Stub。
- 不做 HTTP。

---

## 任务四十：请求号与 `TinyPbRpcController` 语义补齐

**类型**：必须复刻

### 学习目标

理解 RPC 框架层状态和业务 response 的区别。`RpcController` 记录框架层错误、请求号、超时等信息；业务错误应该放在业务 response 中。

### 实现目标

- 新增统一请求号生成工具。
- `TinyPbRpcController` 支持 `reqId`、错误码、错误文本、timeout 占位。
- Channel 在 controller 未设置 `reqId` 时自动生成。
- response `reqId` 与 request 不匹配时失败。

### 关键文件

- `mytinyrpc/comm/reqid.h`
- `mytinyrpc/comm/reqid.cc`
- `mytinyrpc/comm/errorcode.h`
- `mytinyrpc/net/tinypb/tinypbrpccontroller.h`
- `mytinyrpc/net/tinypb/tinypbrpccontroller.cc`
- `mytinyrpc/net/tinypb/tinypbrpcchannel.cc`
- `testcases/test_req_id.cc`
- `testcases/test_tinypb_rpc_channel.cc`

### 建议最小接口

```cpp
std::string ReqIdUtil::genReqId();

void TinyPbRpcController::SetReqId(const std::string& reqId);
const std::string& TinyPbRpcController::ReqId() const;

void TinyPbRpcController::SetError(int code, const std::string& info);
int TinyPbRpcController::ErrorCode() const;
std::string TinyPbRpcController::ErrorText() const override;

void TinyPbRpcController::SetTimeout(int timeoutMs);
int TinyPbRpcController::Timeout() const;
```

### 测试方式

- 请求号连续生成，不为空、不重复。
- controller `Reset()` 清空错误和请求号。
- Channel 自动生成请求号。
- mock server 返回不匹配的 `reqId`。

### 验收标准

- `test_req_id` 通过。
- `test_tinypb_rpc_channel` 新增 reqId mismatch 测试通过。
- controller 能稳定表达错误码和错误文本。

### 不包括

- 不做真正定时器超时。
- 不做多路复用响应缓存。
- 不做异步 pending map。

---

## 任务四十一：同步客户端超时与失败路径

**类型**：简化复刻

### 学习目标

理解同步客户端在连接失败、读写失败、服务端提前关闭、服务端不返回时应该如何表达错误。

### 实现目标

- 为同步 `TcpClient` 增加连接超时和读写超时的最小实现。
- Channel 将 controller timeout 传递给 client。
- 失败时返回明确错误码和错误文本。

### 关键文件

- `mytinyrpc/net/tcpclient.h`
- `mytinyrpc/net/tcpclient.cc`
- `mytinyrpc/comm/errorcode.h`
- `testcases/test_tcp_client.cc`
- `testcases/test_tinypb_rpc_channel.cc`

### 测试方式

- 连接不存在端口，验证连接失败。
- mock server accept 后不返回，验证读超时。
- mock server 提前关闭，验证 recv 失败。
- mock server 慢响应但未超过 timeout，验证成功。

### 验收标准

- 每种失败都有明确错误码。
- controller 能拿到错误文本。
- 已有同步 RPC 正常路径不回退。

### 不包括

- 不做异步重试。
- 不做连接池。
- 不做客户端 Reactor 化。

---

## 任务四十二：阶段八调用链文档和同步 RPC 回归脚本

**类型**：必须复刻

### 学习目标

通过文档把同步 RPC 主链路讲清楚，避免“代码能跑但不理解”。

### 实现目标

- 补齐 `docs/stage-8.md`。
- 新增同步 RPC 回归脚本。
- 更新 `docs/replica-progress.md`。

### 关键文件

- `docs/stage-8.md`
- `docs/replica-progress.md`
- `scripts/check_rpc_sync_basic.sh`
- `README.md`

### 测试方式

脚本串联运行：

- build
- TinyPB codec 测试
- TinyPB dispatcher 测试
- TcpClient 测试
- TinyPbRpcChannel 测试
- 端到端同步 RPC 测试

### 验收标准

- 脚本输出 `[rpc-sync-basic] PASS`。
- 文档包含同步 RPC 调用链图。
- 文档明确当前不支持异步、连接池、多路复用。

### 不包括

- 不做最终 README 长文。
- 不做性能测试。

---

# 阶段九：同步客户端连接语义收口

## 阶段目标

把同步 RPC 客户端从“能调用”提升到“错误边界清楚、可回归、连接状态可解释”。

## 阶段完成标准

- `TcpClient` 的连接失败、关闭、重连、超时语义清楚。
- 错误码矩阵清楚区分框架错误、TinyPB 错误和业务错误。
- 同步 RPC 回归脚本稳定。
- 暂不引入响应缓存和并发 pending map。

---

## 任务四十三：`TcpClient` 重连和关闭边界

**类型**：简化复刻

### 学习目标

理解同步客户端 fd 的创建、关闭、复用和失败恢复边界。

### 实现目标

- `closeConnection()` 后允许再次请求并重新连接。
- 连接失败后允许重新创建 fd。
- 支持有限重试次数。

### 关键文件

- `mytinyrpc/net/tcpclient.h`
- `mytinyrpc/net/tcpclient.cc`
- `testcases/test_tcp_client.cc`
- `docs/stage-9.md`

### 测试方式

- 第一次连接失败，第二次服务端启动后成功。
- 重试次数耗尽后失败。
- 主动 `closeConnection()` 后再次请求成功。

### 验收标准

- fd 状态不会混乱。
- 重试行为有日志或错误文本可观察。
- 同步 RPC 正常路径不回退。

### 不包括

- 不做后台自动重连。
- 不做连接池。
- 不做多服务节点负载均衡。

---

## 任务四十四：同步客户端错误码矩阵

**类型**：必须复刻

### 学习目标

理解 RPC 框架中的错误分层：网络错误、协议错误、框架错误、业务错误不能混在一起。

### 实现目标

- 整理 `comm/errorcode.h`。
- 补充 `docs/error-code.md`。
- 在测试中覆盖主要错误码。

### 关键文件

- `mytinyrpc/comm/errorcode.h`
- `docs/error-code.md`
- `testcases/test_tcp_client.cc`
- `testcases/test_tinypb_rpc_channel.cc`

### 测试方式

每个核心错误码至少有一个测试触发：

- serialize failed
- deserialize failed
- connect failed
- send failed
- recv failed
- timeout
- reqId mismatch
- server framework error

### 验收标准

- controller error、TinyPB `err_code`、业务 response `ret_code` 三层含义清楚。
- 文档中列出错误码、触发场景、测试用例。

### 不包括

- 不设计业务错误码规范生成器。
- 不做 tracing。

---

## 任务四十五：同步 RPC 稳定性回归脚本

**类型**：必须复刻

### 学习目标

建立后续重构的安全网，防止 Reactor、IOThread、HTTP 改动破坏同步 RPC。

### 实现目标

- 新增 `scripts/check_rpc_sync.sh`。
- 汇总同步 RPC 相关所有测试。

### 关键文件

- `scripts/check_rpc_sync.sh`
- `scripts/check_rpc_sync.ps1`，可选
- `docs/stage-9.md`
- `README.md`

### 测试方式

一键执行：

- 构建
- 单元测试
- mock Channel 测试
- 真实端到端同步 RPC 测试

### 验收标准

- Linux/WSL 下 `./scripts/check_rpc_sync.sh` 输出 `[rpc-sync] PASS`。
- 每次后续阶段完成后都必须跑该脚本。

### 不包括

- 不做压测。
- 不做异步 RPC。

---

## 任务四十六：推迟响应缓存，仅保留 reqId mismatch 检查

**类型**：简化复刻

### 学习目标

明确同步单请求模型和异步多请求模型的差异，避免过早把 pending map 塞进同步客户端。

### 实现目标

- 文档说明：当前同步客户端只支持单 in-flight 请求。
- 如果响应 `reqId` 不匹配，直接失败。
- 不缓存乱序响应。

### 关键文件

- `mytinyrpc/net/tcpclient.cc`
- `mytinyrpc/net/tinypb/tinypbrpcchannel.cc`
- `testcases/test_tinypb_rpc_channel.cc`
- `docs/stage-9.md`

### 测试方式

- mock server 返回错误 `reqId`。
- 客户端失败并设置 `ERROR_RPC_REQID_MISMATCH`。

### 验收标准

- 行为简单、确定、可解释。
- 文档明确响应缓存留到异步 RPC 阶段。

### 不包括

- 不做 pending map。
- 不做并发请求。
- 不做乱序响应缓存。

---

# 阶段十：Timer、Reactor wakeup 和连接生命周期

## 阶段目标

让 Reactor 从“能处理 fd 事件”升级到“能处理时间事件、跨线程任务投递和安全退出”。

## 阶段完成标准

- `TimerEvent` 可表达一次性/重复定时任务。
- `Timer` 通过 `timerfd` 接入 Reactor。
- Reactor 支持 `addTask()`、`wakeup()`、`stop()`。
- TcpConnection 有最小空闲超时管理。
- 文档能解释 fd event、timerfd、wakeup、连接关闭路径。

---

## 任务四十七：`TimerEvent` 与基础时间函数

**类型**：必须复刻

### 学习目标

先理解定时任务对象本身，再接入 `timerfd` 和 Reactor。

### 实现目标

- 新增 `getNowMs()`。
- 新增 `TimerEvent`。
- 支持一次性任务、重复任务、cancel、reset。

### 关键文件

- `mytinyrpc/net/timer.h`
- `mytinyrpc/net/timer.cc`
- `testcases/test_timer_event.cc`
- `CMakeLists.txt`
- `docs/stage-10.md`

### 测试方式

- 创建一次性任务，到期时间大于当前时间。
- `resetTime()` 刷新到期时间。
- cancel 后任务不执行。
- repeated 任务重置下一次到期时间。

### 验收标准

- 内存级测试通过。
- 不依赖 Reactor 也能验证 TimerEvent 行为。

### 不包括

- 不接入 `timerfd`。
- 不接入 TcpConnection。

---

## 任务四十八：`Timer` + `timerfd` 接入 Reactor

**类型**：必须复刻

### 学习目标

理解 timerfd 的意义：把时间到期变成 fd 可读事件，让 Reactor 统一处理 socket、timer、wakeup。

### 实现目标

- `Timer` 持有或继承 `FdEvent`。
- 使用 `timerfd_create()` 和 `timerfd_settime()`。
- Reactor 持有 Timer。
- 支持添加、删除、重置最近到期任务。

### 关键文件

- `mytinyrpc/net/timer.h`
- `mytinyrpc/net/timer.cc`
- `mytinyrpc/net/reactor.h`
- `mytinyrpc/net/reactor.cc`
- `testcases/test_timer.cc`

### 测试方式

- Reactor loop 中执行一次性任务。
- 重复任务执行多次后取消。
- 多个任务按到期时间触发。
- 删除任务后不触发。

### 验收标准

- `test_timer` 通过。
- timerfd 可被 epoll 唤醒。
- Reactor 不需要额外 sleep 线程。

### 不包括

- 不做连接时间轮。
- 不做协程 sleep hook。

---

## 任务四十九：Reactor 任务队列和 wakeup fd

**类型**：必须复刻

### 学习目标

理解跨线程投递任务为什么需要 wakeup fd。没有 wakeup，IOThread 和异步 RPC 都会出现任务提交后 Reactor 长时间阻塞的问题。

### 实现目标

- Reactor 支持 `addTask()`。
- 增加 wakeup fd，如 `eventfd` 或 pipe。
- `addTask()` 从其他线程调用时能唤醒 `epoll_wait()`。
- `stop()` 能唤醒 loop 并安全退出。

### 关键文件

- `mytinyrpc/net/reactor.h`
- `mytinyrpc/net/reactor.cc`
- `testcases/test_reactor.cc`

### 测试方式

- loop 线程外投递任务，任务能执行。
- 连续投递多个任务，按提交顺序执行。
- loop 阻塞时调用 `stop()`，能被唤醒并退出。

### 验收标准

- `test_reactor` 覆盖跨线程 addTask。
- 不出现死锁。
- stop 不依赖额外网络事件。

### 不包括

- 不做协程调度队列。
- 不做 IOThreadPool。

---

## 任务五十：Reactor 安全退出和事件生命周期回归

**类型**：必须复刻

### 学习目标

明确 fd 注册、事件触发、callback 执行、删除 fd、停止 loop 的完整生命周期。

### 实现目标

- 整理 `addFdEvent()`、`delFdEvent()`、`stop()` 行为。
- 明确 callback 在 Reactor 线程执行。
- 补充事件删除和重复注册测试。

### 关键文件

- `mytinyrpc/net/reactor.h`
- `mytinyrpc/net/reactor.cc`
- `mytinyrpc/net/fdevent.h`
- `mytinyrpc/net/fdevent.cc`
- `testcases/test_reactor.cc`

### 测试方式

- 注册 fd，可读后触发回调。
- 删除 fd 后不再触发。
- 重复注册同一 fd 行为明确。
- callback 中调用 `stop()` 不死锁。

### 验收标准

- Reactor 行为可预测。
- 文档记录所有 callback 线程归属。

### 不包括

- 不做多 Reactor。
- 不做性能优化。

---

## 任务五十一：连接空闲超时 / 简化时间轮

**类型**：简化复刻

### 学习目标

理解 Timer 如何参与 TcpConnection 生命周期管理。

### 实现目标

- 新增简化 idle timeout 管理器或简化 `TcpConnectionTimeWheel`。
- 连接活跃时刷新超时时间。
- 空闲连接被标记关闭。
- 关闭动作在连接所属 Reactor 中执行。

### 关键文件

- `mytinyrpc/net/tcpconnectiontimewheel.h`
- `mytinyrpc/net/tcpconnectiontimewheel.cc`
- `mytinyrpc/net/tcpconnection.h`
- `mytinyrpc/net/tcpconnection.cc`
- `testcases/test_tcp_timewheel.cc`

### 测试方式

- 活跃连接不超时。
- 空闲连接超时后被标记。
- 超时关闭不跨线程直接操作 fd。

### 验收标准

- 连接超时路径可测试。
- 同步 RPC 回归仍通过。

### 不包括

- 不做复杂分层时间轮。
- 不做跨线程销毁优化。

---

## 任务五十二：Reactor / Timer / TcpConnection 调试文档

**类型**：必须复刻

### 学习目标

用文档固化单线程事件模型，为多线程 IOThread 做准备。

### 实现目标

- 新增 `docs/stage-10.md`。
- 画出 fd event 生命周期图。
- 画出 timerfd 触发路径。
- 画出 wakeup 任务投递路径。
- 画出 TcpConnection 关闭路径。

### 关键文件

- `docs/stage-10.md`
- `docs/reactor-event-lifecycle.md`
- `docs/tcpconnection-lifetime.md`，可先建立草稿

### 测试方式

- 运行 `check_rpc_sync.sh`。
- 运行 Timer 和 Reactor 单测。

### 验收标准

- 文档能回答：callback 在哪个线程执行？fd 由谁关闭？Timer 如何唤醒 Reactor？stop 如何退出？

### 不包括

- 不写最终总体设计。

---

# 阶段十一：IOThread 与服务端多 Reactor

## 阶段目标

实现 Main Reactor accept，Sub Reactor 处理连接读写的服务端模型。

## 阶段完成标准

- 每个 IOThread 持有一个 Reactor。
- IOThreadPool 可 round-robin 分配连接。
- TcpServer 可把新连接分发到 Sub Reactor。
- TcpConnection 的创建、注册、读写、关闭、析构线程归属清楚。
- 多客户端同步 RPC 回归通过。

---

## 任务五十三：`Mutex`、`RWMutex` 和基础线程工具

**类型**：简化复刻

### 学习目标

理解多 Reactor 前需要哪些最小同步原语。

### 实现目标

- 封装 `Mutex` 和 `RWMutex`。
- 可基于 `std::mutex`、`std::shared_mutex` 实现。
- 为 Reactor task queue 和 IOThreadPool 准备基础工具。

### 关键文件

- `mytinyrpc/net/mutex.h`
- `mytinyrpc/net/mutex.cc`
- `testcases/test_mutex.cc`

### 测试方式

- 多线程递增计数验证互斥。
- 多读单写基本测试。

### 验收标准

- 不死锁。
- 行为清楚。

### 不包括

- 不追求完全复刻原项目锁实现。
- 不做复杂无锁结构。

---

## 任务五十四：`IOThread` 生命周期

**类型**：必须复刻

### 学习目标

理解一个线程一个 Reactor 的模型。

### 实现目标

- 新增 `IOThread`。
- 线程启动后进入 Reactor loop。
- 提供 `getReactor()`、`addTask()`、`stop()`。

### 关键文件

- `mytinyrpc/net/iothread.h`
- `mytinyrpc/net/iothread.cc`
- `mytinyrpc/net/reactor.h`
- `testcases/test_iothread.cc`

### 测试方式

- 创建 IOThread。
- 向 IOThread 投递任务。
- 验证任务在 IOThread 所在线程执行。
- stop 后线程退出。

### 验收标准

- 析构不泄露线程。
- 不出现 join 死锁。

### 不包括

- 不接入 TcpServer。
- 不做线程池。

---

## 任务五十五：`IOThreadPool`

**类型**：必须复刻

### 学习目标

理解多个 Sub Reactor 的管理和分配策略。

### 实现目标

- 管理多个 IOThread。
- round-robin 获取下一个 IOThread。
- 支持 broadcast task。
- 支持指定 index 投递任务。

### 关键文件

- `mytinyrpc/net/iothreadpool.h`
- `mytinyrpc/net/iothreadpool.cc`
- `mytinyrpc/net/iothread.h`
- `testcases/test_iothreadpool.cc`

### 测试方式

- pool size 为 2 时连续获取线程轮转。
- broadcast 每个线程执行一次。
- 指定 index 投递只在目标线程执行。

### 验收标准

- IOThreadPool 可稳定启动和停止。
- 任务线程归属可验证。

### 不包括

- 不做动态扩缩容。
- 不做复杂负载均衡。

---

## 任务五十六：`TcpServer` 接入 IOThreadPool

**类型**：必须复刻

### 学习目标

理解 Main Reactor 和 Sub Reactor 分工，以及连接 fd 的线程归属。

### 实现目标

- Main Reactor 只负责 accept。
- 新连接分发到 IOThreadPool 中的 Sub Reactor。
- TcpConnection 在所属 Sub Reactor 中注册读写事件。
- 关闭连接也投递回所属 Reactor 执行。

### 关键文件

- `mytinyrpc/net/tcpserver.h`
- `mytinyrpc/net/tcpserver.cc`
- `mytinyrpc/net/tcpconnection.h`
- `mytinyrpc/net/tcpconnection.cc`
- `mytinyrpc/net/iothreadpool.h`
- `scripts/check_stage11_server.sh`

### 测试方式

- 多客户端并发同步 RPC。
- 验证请求分散到不同 IOThread。
- 同步 RPC 回归脚本仍通过。

### 验收标准

- `./scripts/check_stage11_server.sh` 输出 `[stage11] PASS`。
- 不出现跨线程直接操作 fd 的隐患。
- TcpServer 单线程模式仍可用。

### 不包括

- 不做异步客户端。
- 不做 HTTP。
- 不做高性能压测。

---

## 任务五十七：TcpConnection 所有权和状态机文档

**类型**：必须复刻

### 学习目标

多线程 bug 大多来自对象所有权和线程归属不清。本任务只写文档，但非常关键。

### 实现目标

- 新增或完善 `docs/tcpconnection-lifetime.md`。
- 记录 TcpConnection 创建、注册、读、写、关闭、析构的线程归属。
- 记录 fd、input buffer、output buffer、dispatcher 的关系。

### 关键文件

- `docs/tcpconnection-lifetime.md`
- `docs/stage-11.md`

### 测试方式

- 跑 `check_rpc_sync.sh`。
- 跑 `check_stage11_server.sh`。
- 对照文档检查实现是否一致。

### 验收标准

- 文档能回答：连接对象由谁持有？fd 由谁关闭？回调在哪个线程执行？

### 不包括

- 不做内存池优化。

---

# 阶段十二：HTTP 协议栈

## 阶段目标

在现有 TcpServer / Codec / Dispatcher 模型上增加 HTTP 支持。

## 阶段完成标准

- HTTP request/response 数据结构可用。
- HTTP codec 支持 GET、POST、header、Content-Length 和半包。
- HttpDispatcher 可按 path 分发。
- TcpServer 可运行 HTTP server 示例。
- TinyPB RPC 回归不受影响。

---

## 任务五十八：HTTP 基础数据结构

**类型**：简化复刻

### 学习目标

理解 HTTP message 的基本组成：method、path、version、status、headers、body。

### 实现目标

- 新增 HTTP method、status code、header 类型。
- 新增 `HttpRequest`。
- 新增 `HttpResponse`。

### 关键文件

- `mytinyrpc/net/http/httpdefine.h`
- `mytinyrpc/net/http/httpdefine.cc`
- `mytinyrpc/net/http/httprequest.h`
- `mytinyrpc/net/http/httprequest.cc`
- `mytinyrpc/net/http/httpresponse.h`
- `mytinyrpc/net/http/httpresponse.cc`
- `testcases/test_httpdefine.cc`

### 测试方式

- `httpCodeToString(200)` 返回 `OK`。
- header 可设置和读取。
- response 可生成状态行、header、body。

### 验收标准

- 数据结构测试通过。
- 不依赖网络即可验证。

### 不包括

- 不做 parser。
- 不做 chunked。
- 不做 keep-alive 完整语义。

---

## 任务五十九：HTTP 请求解码

**类型**：必须复刻

### 学习目标

训练协议 parser 和半包处理能力。

### 实现目标

- 新增 `HttpCodec::decode()`。
- 解析 request line。
- 解析 headers。
- 按 `Content-Length` 读取 body。
- 数据不完整时不误判成功。

### 关键文件

- `mytinyrpc/net/http/httpcodec.h`
- `mytinyrpc/net/http/httpcodec.cc`
- `mytinyrpc/net/tcpbuffer.h`
- `testcases/test_http_codec.cc`

### 测试方式

- GET 请求解析。
- POST 请求解析。
- 半包第一次 decode 不成功，补齐后成功。
- 非法 request line 返回失败。

### 验收标准

- `test_http_codec` 覆盖正常包、半包、非法包。
- parser 不越界、不死循环。

### 不包括

- 不做 multipart。
- 不做 chunked。
- 不做 HTTP/2。

---

## 任务六十：HTTP 响应编码

**类型**：必须复刻

### 学习目标

完成 HTTP 请求-响应协议闭环。

### 实现目标

- 实现 `HttpCodec::encode()`。
- 将 `HttpResponse` 编码到 `TcpBuffer`。
- 自动设置或校验 `Content-Length`。

### 关键文件

- `mytinyrpc/net/http/httpcodec.cc`
- `mytinyrpc/net/http/httpresponse.h`
- `testcases/test_http_codec.cc`

### 测试方式

- 200 response 编码。
- 404 response 编码。
- body 和 Content-Length 匹配。

### 验收标准

- 编码出的字符串可被简单 HTTP client 或 curl 理解。

### 不包括

- 不做 gzip。
- 不做 streaming response。

---

## 任务六十一：HttpServlet 与 HttpDispatcher

**类型**：必须复刻

### 学习目标

理解 HTTP 路由分发和 RPC method 分发的区别。

### 实现目标

- 新增 `HttpServlet` 抽象类。
- 新增 `NotFoundHttpServlet`。
- 新增 `HttpDispatcher`。
- 根据 path 分发请求。

### 关键文件

- `mytinyrpc/net/http/httpservlet.h`
- `mytinyrpc/net/http/httpservlet.cc`
- `mytinyrpc/net/http/httpdispatcher.h`
- `mytinyrpc/net/http/httpdispatcher.cc`
- `testcases/test_http_dispatcher.cc`

### 测试方式

- 注册 `/hello` servlet。
- 请求 `/hello` 返回业务 body。
- 请求未知 path 返回 404。

### 验收标准

- dispatcher 单测通过。
- servlet 能设置 status、content-type、body。

### 不包括

- 不做正则路由。
- 不做中间件。
- 不做静态文件服务。

---

## 任务六十二：HTTP Server 集成和脚本

**类型**：必须复刻

### 学习目标

把 HTTP codec/dispatcher 接入 TcpServer，验证框架的协议可插拔性。

### 实现目标

- TcpServer 支持选择 HTTP codec + HTTP dispatcher。
- 新增最小 HTTP server 示例。
- 新增 HTTP 验收脚本。

### 关键文件

- `testcases/test_http_server.cc`
- `scripts/check_stage12_http.sh`
- `docs/stage-12.md`
- `mytinyrpc/net/tcpserver.h`
- `mytinyrpc/net/tcpserver.cc`

### 测试方式

- `curl http://127.0.0.1:19999/hello` 返回预期 body。
- 未知 path 返回 404。
- TinyPB RPC 回归仍通过。

### 验收标准

- `./scripts/check_stage12_http.sh` 输出 `[stage12] PASS`。
- HTTP 和 TinyPB 能共用 TcpServer 抽象。

### 不包括

- 不做 HTTPS。
- 不做 HTTP/2。
- 不做压力测试。

---

# 阶段十三：配置、日志、启动入口和运行时

## 阶段目标

把测试程序升级为更接近框架使用方式的启动模型。

## 阶段完成标准

- 配置对象能提供默认值并读取 XML。
- 日志能输出到文件并支持基本级别。
- 用户可以通过启动入口和注册宏启动 TinyPB 或 HTTP server。
- RunTime 能保存当前请求号等最小上下文。

---

## 任务六十三：最小 Config 默认值整理

**类型**：简化复刻

### 学习目标

减少硬编码端口、timeout、IOThread 数量等散落在测试中的问题。

### 实现目标

- 新增 `Config`。
- 提供默认 server addr、protocol、iothread_num、timeout、log level。
- 测试程序可从 Config 读取默认值。

### 关键文件

- `mytinyrpc/comm/config.h`
- `mytinyrpc/comm/config.cc`
- `testcases/test_config.cc`

### 测试方式

- 缺省 Config 可启动测试 server。
- 每个字段默认值明确。

### 验收标准

- 不读取 XML 也能使用配置对象。
- 默认值写入文档。

### 不包括

- 不读 XML。
- 不做复杂校验。

---

## 任务六十四：XML 配置读取

**类型**：简化复刻

### 学习目标

对齐原项目启动方式，但只读取当前真正需要的字段。

### 实现目标

- Config 支持读取 XML。
- 支持 server addr、protocol、iothread_num、timeout、log 等字段。
- 缺失字段使用默认值。

### 关键文件

- `mytinyrpc/comm/config.h`
- `mytinyrpc/comm/config.cc`
- `conf/test_tinypb_server.xml`
- `conf/test_http_server.xml`
- `testcases/test_config.cc`

### 测试方式

- 合法 XML 读取成功。
- 缺字段走默认值。
- 非法路径返回失败。
- 非法字段类型有明确错误。

### 验收标准

- TinyPB server 和 HTTP server 都能从 XML 读取配置。

### 不包括

- 不复刻 MySQL 插件配置。
- 不做复杂 schema 校验。

---

## 任务六十五：日志系统分步实现

**类型**：简化复刻

### 学习目标

日志是调试 RPC 框架的基础，但异步滚动不是最早目标。先保证日志可用，再做异步增强。

### 实现目标

- 先实现同步文件日志。
- 支持 DEBUG、INFO、WARN、ERROR。
- 支持日志格式中带时间、线程 id、文件行号、reqId。
- 再可选实现异步队列 flush。

### 关键文件

- `mytinyrpc/comm/log.h`
- `mytinyrpc/comm/log.cc`
- `testcases/test_log.cc`

### 测试方式

- 级别过滤正确。
- 文件输出正确。
- flush 后文件可读。
- 关闭日志时不输出。
- 异步模式下日志最终落盘。

### 验收标准

- RPC 调试时能看到请求号、方法名、错误码。
- 同步模式稳定可用。

### 不包括

- 第一版不强制按大小滚动。
- 不做复杂日志压缩。

---

## 任务六十六：启动入口和服务注册宏

**类型**：必须复刻

### 学习目标

让框架使用方式从“手动拼测试对象”升级为“配置 + 注册 + 启动”。

### 实现目标

- 新增 `InitConfig()`。
- 新增 `StartRpcServer()`。
- 新增 `GetServer()`，可选。
- 新增 `REGISTER_SERVICE()`。
- 新增 `REGISTER_HTTP_SERVLET()`。

### 关键文件

- `mytinyrpc/comm/start.h`
- `mytinyrpc/comm/start.cc`
- `mytinyrpc/comm/runtime.h`
- `mytinyrpc/comm/runtime.cc`
- `testcases/test_start.cc`
- `README.md`

### 测试方式

- 使用 XML 启动 TinyPB server。
- `REGISTER_SERVICE(QueryServiceImpl)` 能注册服务。
- 使用 XML 启动 HTTP server。
- `REGISTER_HTTP_SERVLET("/hello", HelloServlet)` 能注册 servlet。

### 验收标准

- 示例程序入口明显变短。
- 用户能按 README 启动一个最小 RPC server。

### 不包括

- 不做插件系统。
- 不做复杂生命周期管理器。

---

## 任务六十七：运行时 request context

**类型**：简化复刻

### 学习目标

理解为什么日志、controller、dispatcher 需要一个请求级上下文。

### 实现目标

- `RunTime` 保存当前请求号。
- 保存当前 method name。
- 保存简化 local/peer addr。
- dispatcher 处理请求时设置上下文。
- 日志可读取当前 reqId。

### 关键文件

- `mytinyrpc/comm/runtime.h`
- `mytinyrpc/comm/runtime.cc`
- `mytinyrpc/net/tinypb/tinypbdispatcher.cc`
- `mytinyrpc/comm/log.cc`
- `testcases/test_runtime.cc`

### 测试方式

- 服务端处理请求时能读取当前 reqId。
- 请求结束后上下文清理。
- 多线程请求上下文互不污染。

### 验收标准

- 日志中能稳定打印 reqId。
- RunTime 不引入跨请求污染。

### 不包括

- 不做完整 tracing。
- 不做分布式链路追踪。

---

# 阶段十四：协程、hook、协程池和内存池整理

## 阶段目标

把已有最小协程能力整理成可解释、可测试、可用于后续异步 RPC 的基础。

## 阶段完成标准

- 文档能解释 yield/resume 和 hook 的关系。
- `connect` hook 可用。
- `sleep/usleep` hook 可用。
- `recv/send/accept` hook 有简化实现或明确暂缓。
- CoroutinePool 可选实现。
- 内存池不阻塞主线。

---

## 任务六十八：协程现状梳理和调试文档

**类型**：必须复刻

### 学习目标

先确认已有 coroutine、readHook、writeHook 的边界，不急着扩展 API。

### 实现目标

- 新增 `docs/coroutine-model.md`。
- 梳理 `Coroutine` 状态。
- 梳理 hook 开关。
- 梳理 fd event 恢复协程的路径。

### 关键文件

- `mytinyrpc/coroutine/coroutine.h`
- `mytinyrpc/coroutine/coroutine.cc`
- `mytinyrpc/coroutine/coroutinehook.h`
- `mytinyrpc/coroutine/coroutinehook.cc`
- `docs/coroutine-model.md`

### 测试方式

- 运行已有协程测试。
- 运行服务端读写 hook 回归。

### 验收标准

文档能回答：

- 协程如何创建？
- 何时 yield？
- 何时 resume？
- hook 如何找到当前 Reactor？
- hook 关闭时走什么路径？

### 不包括

- 不新增复杂 hook。
- 不做协程池。

---

## 任务六十九：`connect` hook

**类型**：简化复刻

### 学习目标

理解非阻塞 connect + Reactor 事件 + timeout + 协程恢复。

### 实现目标

- hook `connect()`。
- 对非阻塞连接中的 fd 注册写事件。
- 连接成功或失败后恢复当前协程。
- 支持 timeout。

### 关键文件

- `mytinyrpc/coroutine/coroutinehook.h`
- `mytinyrpc/coroutine/coroutinehook.cc`
- `mytinyrpc/net/reactor.h`
- `mytinyrpc/net/timer.h`
- `testcases/test_hook.cc`

### 测试方式

- 连接成功。
- 连接拒绝。
- 连接超时。
- hook 关闭时走原始 connect。

### 验收标准

- hook 开启时不阻塞 Reactor 线程。
- 超时能恢复协程并返回失败。

### 不包括

- 不做 DNS hook。
- 不做所有 socket option。

---

## 任务七十：`sleep/usleep` hook

**类型**：简化复刻

### 学习目标

通过最简单的阻塞函数理解 TimerEvent 如何恢复协程。

### 实现目标

- hook `sleep()`。
- hook `usleep()`。
- 当前协程让出，TimerEvent 到期后恢复。

### 关键文件

- `mytinyrpc/coroutine/coroutinehook.h`
- `mytinyrpc/coroutine/coroutinehook.cc`
- `mytinyrpc/net/timer.h`
- `testcases/test_hook_sleep.cc`

### 测试方式

- 一个协程 sleep 不阻塞另一个协程执行。
- 多个协程 sleep 按时间恢复。

### 验收标准

- Reactor 线程不被真实 sleep 阻塞。
- 恢复时间在合理误差范围内。

### 不包括

- 不追求纳秒级精度。
- 不做复杂定时调度优化。

---

## 任务七十一：`recv/send/accept` hook 补齐

**类型**：简化复刻

### 学习目标

理解常见 socket 阻塞点如何统一交给 Reactor 等待事件。

### 实现目标

- hook `recv()`。
- hook `send()`。
- hook `accept()`。
- EAGAIN 时注册事件并 yield。
- 事件触发或超时后恢复。

### 关键文件

- `mytinyrpc/coroutine/coroutinehook.h`
- `mytinyrpc/coroutine/coroutinehook.cc`
- `testcases/test_hook_socket.cc`

### 测试方式

- recv 无数据时 yield，数据到来后恢复。
- send 缓冲区暂不可写时 yield，之后恢复。
- accept 无连接时 yield，连接到来后恢复。
- hook 关闭时走系统调用。

### 验收标准

- 基本 socket hook 可用。
- 超时路径可恢复。

### 不包括

- 不覆盖所有 flag 组合。
- 不做极限压测。

---

## 任务七十二：CoroutinePool

**类型**：简化复刻

### 学习目标

学习协程对象复用和生命周期管理。

### 实现目标

- 新增 `CoroutinePool`。
- 支持获取协程对象。
- 支持归还协程对象。
- 池耗尽行为明确。

### 关键文件

- `mytinyrpc/coroutine/coroutinepool.h`
- `mytinyrpc/coroutine/coroutinepool.cc`
- `testcases/test_coroutinepool.cc`

### 测试方式

- 从池中取出的协程可运行任务。
- 归还后可复用。
- 池耗尽时返回空或新建，行为写入文档。

### 验收标准

- 协程复用不污染上一次任务状态。

### 不包括

- 不做复杂调度器。
- 不做 work stealing。

---

## 任务七十三：协程栈内存池

**类型**：暂不复刻 / 简化复刻

### 学习目标

理解内存池属于性能优化，不是 RPC 主链路必需能力。

### 实现目标

- 只有在前面阶段稳定后再做。
- 新增固定块内存池。
- 供协程栈复用。

### 关键文件

- `mytinyrpc/coroutine/memory.h`
- `mytinyrpc/coroutine/memory.cc`
- `testcases/test_memory_pool.cc`

### 测试方式

- 申请固定数量 block。
- 归还后可再次申请。
- 非本池 block 不可归还。

### 验收标准

- 不影响已有协程和 RPC 测试。

### 不包括

- 不让内存池阻塞异步 RPC。
- 不做复杂 slab 分配器。

---

# 阶段十五：异步 RPC Channel

## 阶段目标

从同步 Stub 调用升级到异步 RPC 调用，并能处理并发、超时和回调生命周期。

## 阶段完成标准

- 异步 Channel 能保存 request/response/controller/closure 生命周期。
- pending map 能按 reqId 匹配响应。
- IOThread/Reactor 能驱动异步网络读写。
- 超时请求能清理并触发回调。
- 异步端到端示例可运行。

---

## 任务七十四：异步 Channel 生命周期外壳

**类型**：必须复刻

### 学习目标

先解决异步调用中对象生命周期问题，再追求真正网络异步。

### 实现目标

- 新增 `TinyPbRpcAsyncChannel`。
- 保存 controller、request、response、closure。
- 先可内部复用同步 Channel，跑通异步接口外壳。
- 保证成功或失败时 `done` 都会执行。

### 关键文件

- `mytinyrpc/net/tinypb/tinypbrpcasyncchannel.h`
- `mytinyrpc/net/tinypb/tinypbrpcasyncchannel.cc`
- `testcases/test_tinypb_rpc_async_channel.cc`

### 测试方式

- `CallMethod()` 能触发 done closure。
- controller 失败时 closure 仍可观察错误。
- request/response 在 closure 执行前不被释放。

### 验收标准

- 异步接口外壳可用。
- 生命周期不悬垂。

### 不包括

- 不做真正并发网络 IO。
- 不做 pending map。

---

## 任务七十五：异步请求表和 reqId 匹配

**类型**：必须复刻

### 学习目标

理解为什么异步 RPC 必须有 pending request 表。

### 实现目标

- 维护 `reqId -> callback/context` 映射。
- 发送请求前注册 pending。
- 响应回来后按 reqId 找到对应上下文。
- 支持乱序响应。

### 关键文件

- `mytinyrpc/net/tinypb/tinypbrpcasyncchannel.h`
- `mytinyrpc/net/tinypb/tinypbrpcasyncchannel.cc`
- `testcases/test_tinypb_rpc_async_channel.cc`

### 测试方式

- 发出多个请求。
- mock server 乱序返回响应。
- 每个 response 匹配正确 request。

### 验收标准

- pending 表新增、命中、删除行为正确。
- 未知 reqId 响应被丢弃或记录错误。

### 不包括

- 不把 pending map 放回同步 TcpClient。
- 不做连接池。

---

## 任务七十六：异步 Channel 接入 IOThread/Reactor

**类型**：必须复刻

### 学习目标

理解真正异步 RPC 的网络事件由 IOThread/Reactor 驱动，而不是调用线程阻塞等待。

### 实现目标

- 异步请求投递到 IOThread。
- IOThread 负责网络连接、发送和读取响应。
- 响应回来后执行 closure 或投递 closure。
- 明确 closure 执行线程。

### 关键文件

- `mytinyrpc/net/tinypb/tinypbrpcasyncchannel.cc`
- `mytinyrpc/net/iothread.h`
- `mytinyrpc/net/iothreadpool.h`
- `mytinyrpc/net/reactor.h`
- `testcases/test_tinypb_rpc_async_channel.cc`

### 测试方式

- 10 个并发异步请求全部成功。
- 一个请求失败不影响其他请求。
- closure 执行线程可观察。

### 验收标准

- 异步 RPC 不阻塞调用线程。
- IOThread 可安全停止。

### 不包括

- 不做复杂连接池策略。
- 不做自动负载均衡。

---

## 任务七十七：异步超时和取消

**类型**：简化复刻

### 学习目标

理解异步请求失败时如何清理 pending，并避免迟到响应二次回调。

### 实现目标

- 每个异步请求注册 TimerEvent。
- 超时后从 pending map 删除。
- 设置 controller 错误。
- 执行回调。
- 迟到响应被丢弃或记录日志。

### 关键文件

- `mytinyrpc/net/tinypb/tinypbrpcasyncchannel.cc`
- `mytinyrpc/net/timer.h`
- `mytinyrpc/comm/errorcode.h`
- `testcases/test_tinypb_rpc_async_channel.cc`

### 测试方式

- 请求超时后 callback 执行。
- 超时后 pending map 清理。
- 迟到响应不会再次触发 callback。

### 验收标准

- 无二次回调。
- 无 pending 泄露。

### 不包括

- 不做复杂取消传播。
- 不做 retry policy。

---

## 任务七十八：异步 RPC 调用链文档和回归脚本

**类型**：必须复刻

### 学习目标

异步最容易“能跑但不懂”，必须通过文档固化生命周期。

### 实现目标

- 新增 `docs/stage-15.md`。
- 新增 `scripts/check_rpc_async.sh`。
- 画出异步请求生命周期图。

### 关键文件

- `docs/stage-15.md`
- `scripts/check_rpc_async.sh`
- `testcases/test_tinypb_async_client.cc`

### 测试方式

- 一键启动服务端。
- 运行异步客户端。
- 并发请求、超时请求、服务端错误都覆盖。

### 验收标准

- `./scripts/check_rpc_async.sh` 输出 `[rpc-async] PASS`。
- 文档说明 request 发出、pending 注册、响应匹配、timeout、closure 执行线程。

### 不包括

- 不写性能报告。

---

# 阶段十六：代码生成器与示例工程

## 阶段目标

让框架具备生成业务工程骨架的能力，但不被生成器复杂度绑架。

## 阶段完成标准

- 生成器 CLI 可用。
- 可复制模板生成最小工程。
- 可从简单 proto 识别 service/method 并生成骨架。
- 生成工程能构建、启动、调用、关闭。

---

## 任务七十九：生成器 CLI 和模板复制

**类型**：简化复刻

### 学习目标

先理解生成器的输入输出，不急着做完整 proto parser。

### 实现目标

- 新增 `tinyrpc_generator.py`。
- 支持 proto 文件、服务名、输出目录参数。
- 复制固定模板到输出目录。

### 关键文件

- `generator/tinyrpc_generator.py`
- `generator/template/*`
- `scripts/check_generator.sh`

### 测试方式

- 执行生成器后生成 `conf.xml`、`main.cc`、`server.h/cc`、`client.cc`、`run.sh`、`shutdown.sh`。
- 输出目录不存在时自动创建。

### 验收标准

- 模板复制稳定。
- 参数错误有明确提示。

### 不包括

- 不解析复杂 proto。
- 不做多语言生成。

---

## 任务八十：proto service/method 骨架生成

**类型**：简化复刻

### 学习目标

让生成结果和 Protobuf 服务定义对应。

### 实现目标

- 从简单 proto 中识别 service。
- 识别 rpc method。
- 生成业务实现占位类。
- 生成客户端调用模板。

### 关键文件

- `generator/tinyrpc_generator.py`
- `generator/template/interface.h.template`
- `generator/template/interface.cc.template`
- `generator/template/client.cc.template`
- `testcases/test_tinypb_server.proto`

### 测试方式

- 用 `test_tinypb_server.proto` 生成工程。
- 生成代码能编译。
- 生成客户端能调用 QueryService。

### 验收标准

- 简单 service/method 可正确生成。
- 模板中 include 和命名空间正确。

### 不包括

- 不做完整 Protobuf parser。
- 可限制 proto 格式。

---

## 任务八十一：生成工程端到端验收

**类型**：必须复刻

### 学习目标

验证生成器不是摆设，而是真的能生成可运行工程。

### 实现目标

- 自动生成独立示例工程。
- 构建。
- 启动服务端。
- 运行客户端。
- 关闭服务端。

### 关键文件

- `scripts/check_generator_project.sh`
- `docs/stage-16.md`
- `generator/template/README.md.template`

### 测试方式

- 脚本自动完成生成、构建、启动、调用、关闭。

### 验收标准

- `./scripts/check_generator_project.sh` 输出 `[generator] PASS`。
- 生成工程 README 说明如何运行。

### 不包括

- 不做 IDE 工程生成。
- 不做复杂脚手架交互。

---

# 阶段十七：工程收口、覆盖矩阵和最终文档

## 阶段目标

把学习型复刻项目整理成可读、可跑、可对照、可维护的完整项目。

## 阶段完成标准

- 目录结构清楚。
- 全量回归脚本通过。
- 有原 TinyRPC 功能覆盖矩阵。
- 有最终示例和学习总结。
- README 能指导新人跑通核心能力。

---

## 任务八十二：目录和命名兼容整理

**类型**：简化复刻

### 学习目标

减少长期维护混乱，但避免大规模无意义改名。

### 实现目标

- 整理 `comm`、`coroutine`、`net`、`net/http`、`net/tinypb`、`generator`、`conf`。
- 保持旧 include 兼容或提供迁移说明。
- 清理过期测试和临时文件。

### 关键文件

- `CMakeLists.txt`
- `mytinyrpc/**`
- `testcases/**`
- `README.md`

### 测试方式

- 全量构建。
- 所有测试仍通过。

### 验收标准

- 目录结构可解释。
- 不因为整理破坏 API。

### 不包括

- 不做大规模风格重写。

---

## 任务八十三：原 TinyRPC 功能覆盖矩阵

**类型**：必须复刻

### 学习目标

明确哪些功能已经复刻，哪些功能简化，哪些功能暂不复刻。

### 实现目标

- 新增 `docs/original-coverage-matrix.md`。
- 按原项目模块建立覆盖矩阵。

### 关键文件

- `docs/original-coverage-matrix.md`
- `docs/future-replica-plan.md`

### 覆盖模块

- `comm/config`
- `comm/log`
- `comm/start`
- `comm/runtime`
- `coroutine`
- `coroutinepool`
- `net/reactor`
- `net/timer`
- `net/tcp`
- `net/http`
- `net/tinypb`
- `generator`

### 测试方式

- 对照项目目录和测试脚本逐项填写。

### 验收标准

- 每项标注：已复刻、简化复刻、暂不复刻。
- 每项写清楚理由和验证方式。

### 不包括

- 不追求 100% 行为一致。

---

## 任务八十四：一键全量回归

**类型**：必须复刻

### 学习目标

建立最终安全网，确保项目可长期演进。

### 实现目标

- 新增 `scripts/check_all.sh`。
- 新增 `scripts/check_all.ps1`，可选。
- 串联构建、单元测试、同步 RPC、异步 RPC、HTTP、生成器。

### 关键文件

- `scripts/check_all.sh`
- `scripts/check_all.ps1`
- `README.md`

### 测试方式

一键运行：

- build
- codec tests
- dispatcher tests
- tcpclient tests
- sync rpc tests
- reactor/timer tests
- iothread tests
- http tests
- async rpc tests
- generator project tests

### 验收标准

- Linux/WSL 下 `./scripts/check_all.sh` 输出 `[all] PASS`。
- Windows PowerShell 至少完成构建和可运行测试。

### 不包括

- 不强绑定云 CI。
- 不做性能 benchmark。

---

## 任务八十五：最终示例和学习总结

**类型**：必须复刻

### 学习目标

把整个复刻过程沉淀为可复盘、可展示、可继续维护的学习成果。

### 实现目标

- 整理最终 examples。
- 更新 README。
- 编写学习总结。
- 说明从阻塞 Echo 到完整 RPC 框架的演进路径。

### 关键文件

- `examples/tinypb_sync/`
- `examples/tinypb_async/`
- `examples/http_server/`
- `examples/generated_project/`
- `docs/learning-summary.md`
- `README.md`

### 测试方式

- 每个 example 都有构建和运行步骤。
- README 能引导新人跑通核心示例。
- 全量回归通过。

### 验收标准

- 示例覆盖 TinyPB 同步 RPC、TinyPB 异步 RPC、HTTP server、生成器工程。
- 学习总结能对应每个阶段。

### 不包括

- 不写商业级用户手册。
- 不做性能宣传。

---

# 4. 下一次任务执行说明

## 下一次最适合开始的任务

**任务三十八：实现最小 `TinyPbRpcChannel`。**

这个任务直接建立在任务三十七的 `TcpClient::sendAndRecvTinyPb()` 之上，是从“手写 TinyPbStruct 收发”进入“Protobuf Stub 可用”的关键跃迁点。

---

## 4.1 开始前先读哪些文件

建议按下面顺序读：

1. `mytinyrpc/net/tcpclient.h`
2. `mytinyrpc/net/tcpclient.cc`
3. `mytinyrpc/net/tinypb/tinypbdata.h`
4. `mytinyrpc/net/tinypb/tinypbcodec.h`
5. `mytinyrpc/net/tinypb/tinypbcodec.cc`
6. `mytinyrpc/net/tinypb/tinypbdispatcher.h`
7. `mytinyrpc/net/tinypb/tinypbdispatcher.cc`
8. `mytinyrpc/net/tinypb/tinypbrpccontroller.h`
9. `mytinyrpc/net/tinypb/tinypbrpccontroller.cc`
10. `testcases/test_tinypb_dispatcher.cc`
11. `testcases/test_tinypb_server.proto`
12. `testcases/test_tcp_client.cc`
13. `CMakeLists.txt`

读完自己的项目后，再读参考项目的 `tinypb_rpc_channel.h/.cc`，只看设计思路，不逐行照搬。

---

## 4.2 应新增或修改哪些文件

### 新增

- `mytinyrpc/net/tinypb/tinypbrpcchannel.h`
- `mytinyrpc/net/tinypb/tinypbrpcchannel.cc`
- `testcases/test_tinypb_rpc_channel.cc`

### 修改

- `mytinyrpc/net/tinypb/tinypbrpccontroller.h`
- `mytinyrpc/net/tinypb/tinypbrpccontroller.cc`
- `mytinyrpc/comm/errorcode.h`
- `CMakeLists.txt`
- `docs/stage-8.md`
- `docs/replica-progress.md`

---

## 4.3 最小实现接口

```cpp
class TinyPbRpcChannel : public google::protobuf::RpcChannel {
public:
  explicit TinyPbRpcChannel(const IPAddress& peerAddr);

  void CallMethod(
      const google::protobuf::MethodDescriptor* method,
      google::protobuf::RpcController* controller,
      const google::protobuf::Message* request,
      google::protobuf::Message* response,
      google::protobuf::Closure* done) override;

  void setReqIdGenerator(std::function<std::string()> generator);
};
```

`TinyPbRpcController` 先补最小能力：

```cpp
void Reset() override;
bool Failed() const override;
std::string ErrorText() const override;
void SetFailed(const std::string& reason) override;

void SetError(int code, const std::string& info);
int ErrorCode() const;

void SetReqId(const std::string& reqId);
const std::string& ReqId() const;
```

暂不实现或只留占位：

```cpp
SetTimeout();
Timeout();
SetLocalAddr();
SetPeerAddr();
StartCancel();
IsCanceled();
NotifyOnCancel();
```

---

## 4.4 `CallMethod()` 最小行为

`CallMethod()` 只做下面这些事：

1. 检查 `method`、`request`、`response` 是否为空。
2. 构造 TinyPB 请求对象。
3. 设置 `m_serviceFullName = method->full_name()`。
4. 生成或读取 `reqId`。
5. 调用 `request->SerializeToString(&m_pbData)`。
6. 调用 `TcpClient::sendAndRecvTinyPb(req, res)`。
7. 网络失败时设置 controller 错误。
8. `res.m_errCode != 0` 时设置 controller 错误。
9. 调用 `response->ParseFromString(res.m_pbData)`。
10. 反序列化失败时设置 controller 错误。
11. 如果 `done != nullptr`，最后执行 `done->Run()`。

---

## 4.5 测试清单

### 测试一：合法请求和合法响应

- mock server 接收 TinyPB 请求。
- 验证 `serviceFullName` 正确。
- 验证 `reqId` 非空。
- 验证 `pbData` 可 parse 成 request。
- mock server 返回合法 response。
- 客户端 response 字段正确。
- controller 未失败。

### 测试二：服务端返回 TinyPB 错误码

- mock server 返回 `m_errCode != 0`。
- 客户端 controller 进入 failed 状态。
- `ErrorCode()` 和 `ErrorText()` 正确。

### 测试三：服务端返回非法 Protobuf payload

- mock server 返回合法 TinyPB 包，但 `m_pbData` 非法。
- 客户端 response parse 失败。
- controller 记录反序列化错误。

### 测试四：`done` closure 被执行

- 构造 closure 设置 bool 标记。
- 成功路径和失败路径都验证 closure 被执行。

---

## 4.6 任务三十八完成标准

任务三十八完成必须满足：

- `TinyPbRpcChannel` 能被 Protobuf generated Stub 使用。
- `CallMethod()` 能把 Protobuf request 转成 TinyPB 请求。
- mock server 能解出正确 `serviceFullName` 和 request `pbData`。
- Channel 能把 TinyPB response 转回 Protobuf response。
- controller 至少能表达序列化失败、网络失败、服务端错误、反序列化失败。
- `test_tinypb_rpc_channel` 通过。
- 既有 TinyPB codec、dispatcher、TcpClient 测试不回退。
- `docs/stage-8.md` 记录本任务新增能力和限制。

---

## 4.7 完成后应该理解什么

完成任务三十八后，你应该能解释：

> Protobuf Stub 不负责网络通信；`TinyPbRpcChannel` 是 Stub 和 TinyPB/TcpClient 之间的适配层。

更具体地说，你应该理解：

- `MethodDescriptor::full_name()` 为什么可以作为服务端 dispatcher 的路由 key。
- Protobuf request/response 和 TinyPB envelope 的边界在哪里。
- `RpcController` 为什么只表达框架层错误，而不是业务返回值。
- `TcpClient` 在这一层只是“发送 TinyPB 请求并接收 TinyPB 响应”的传输工具。
- 为什么端到端真实 TcpServer 测试应该放在任务三十九，而不是塞进任务三十八。

---

# 5. 建议执行节奏

1. 每个普通任务控制在 1 到 3 个核心文件变化，测试文件可以同任务追加。
2. 每 3 到 5 个任务收口一个阶段文档和一个脚本。
3. 同一任务可以合并“一个小接口 + 一个最小测试 + 一个最小实现”。
4. 不要把跨领域内容塞进同一个任务，例如不要把 HTTP、IOThread、异步 RPC 放进同一任务。
5. 每次开始新任务前执行：
   - 查看最近 git 提交。
   - 读取当前阶段文档。
   - 读取实际源码确认进度。
   - 对齐 `docs/replica-progress.md` 和本计划。
6. 每次完成任务后执行：
   - 构建。
   - 运行该任务测试。
   - 运行相关阶段回归。
   - 更新阶段文档。
   - 记录当前限制。

---

# 6. 总结

当前最重要的路线不是尽快把原 TinyRPC 所有文件搬过来，而是按下面顺序建立理解：

1. 先让 Protobuf Stub 通过 `TinyPbRpcChannel` 完成同步 RPC。
2. 再把请求号、错误码、超时、连接语义讲清楚。
3. 然后补 Reactor 的 Timer、wakeup、task queue。
4. 再进入 IOThreadPool 和服务端多 Reactor。
5. 然后添加 HTTP 协议栈。
6. 再补配置、日志和启动脚手架。
7. 然后整理协程 hook。
8. 最后做异步 RPC、生成器和文档收口。

下一步就从 **任务三十八：实现最小 `TinyPbRpcChannel`** 开始。
