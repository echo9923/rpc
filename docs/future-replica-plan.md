# TinyRPC 完整复刻未来任务计划书

> **用途**：本文档从当前复刻进度继续规划，目标是把 `D:\codeproject\cpp\rpc` 逐步推进到覆盖原始 TinyRPC 项目的主要能力。  
> **当前基线**：截至 2026-05-31，复刻项目已完成到任务三十七：`TcpClient` 具备最小 TinyPB 同步请求/响应闭环。  
> **节奏原则**：任务进度比早期略快，但仍保持每个任务可独立理解、可构建、可测试、可回退。每个阶段完成后补阶段文档和回归脚本。

## 1. 总体复刻路线

后续复刻不再停留在 Echo Server 或单个协议 demo，而是沿着原始 TinyRPC 的完整框架继续推进：

1. 第八阶段：同步 RPC 客户端闭环。
2. 第九阶段：客户端超时、请求号、错误传播与连接语义增强。
3. 第十阶段：定时器、Reactor 任务队列和连接生命周期。
4. 第十一阶段：IO 线程与服务端多 Reactor。
5. 第十二阶段：HTTP 协议栈。
6. 第十三阶段：配置、日志、启动入口和服务注册脚手架。
7. 第十四阶段：协程池、内存池和更完整 hook。
8. 第十五阶段：异步 RPC Channel 与协程化客户端调用。
9. 第十六阶段：代码生成器与示例工程模板。
10. 第十七阶段：工程收口、文档、兼容性比对和完整验收。

## 2. 当前状态摘要

已完成能力：

- 最小构建系统、README、阶段文档和基础验收脚本。
- 阻塞式 TCP Echo Server。
- 非阻塞 fd、`epoll` demo、`FdEvent`、`Reactor`。
- `TcpBuffer`、`TcpConnection` 输入/执行/输出三段式流程。
- 最小协程对象、`read_hook/write_hook` 和服务端读写路径协程化。
- `AbstractData`、`AbstractCodec`、`AbstractDispatcher`。
- TinyPB 数据结构、编码、解码、流式拆包、错误包恢复。
- Protobuf 服务注册、方法查找、服务端 RPC 分发闭环。
- `TcpClient` 连接生命周期和最小 TinyPB 同步收发。

当前缺口：

- 没有 `google::protobuf::RpcChannel` 客户端封装。
- 没有真实客户端 Stub 到服务端的端到端网络 RPC 验收。
- 客户端请求号、超时、错误码、重试和连接语义仍很薄。
- Reactor 还缺完整定时器、跨线程任务投递、wakeup 语义和 IO 线程池。
- HTTP 协议栈尚未复刻。
- 配置、异步日志、启动入口、服务注册宏还未复刻。
- 协程池、内存池、异步 Channel、生成器和最终文档仍未复刻。

## 3. 阶段八：同步 RPC 客户端闭环

### 任务三十八：实现最小 `TinyPbRpcChannel`

**为什么是下一步**：
任务三十七已经能通过 `TcpClient` 发送和接收 `TinyPbStruct`。下一步应把 Protobuf request/response 和 TinyPB 结构串起来，让客户端能通过 `google::protobuf::RpcChannel` 发起调用。

**目标**：

- 新增 `TinyPbRpcChannel`，继承 `google::protobuf::RpcChannel`。
- `CallMethod()` 根据 `MethodDescriptor::full_name()` 填充 `TinyPbStruct::m_serviceFullName`。
- 将 Protobuf request 序列化到 `TinyPbStruct::m_pbData`。
- 调用 `TcpClient::sendAndRecvTinyPb()` 完成同步网络收发。
- 将响应 `m_pbData` 反序列化到 Protobuf response。
- 使用 `TinyPbRpcController` 承载错误信息。

**关键文件**：

- `mytinyrpc/net/tinypb/tinypbrpcchannel.h`
- `mytinyrpc/net/tinypb/tinypbrpcchannel.cc`
- `mytinyrpc/net/tinypb/tinypbrpccontroller.h`
- `mytinyrpc/net/tcpclient.h`
- `testcases/test_tinypb_rpc_channel.cc`
- `CMakeLists.txt`
- `docs/stage-8.md`
- `docs/replica-progress.md`

**建议接口**：

- `tinyrpc::TinyPbRpcChannel::TinyPbRpcChannel(const IPAddress& peerAddr)`
- `void tinyrpc::TinyPbRpcChannel::CallMethod(...)`
- `void tinyrpc::TinyPbRpcChannel::setMsgReqGenerator(std::function<std::string()> generator)`，可选，用于测试固定请求号。
- `const std::string& tinyrpc::TinyPbRpcChannel::getLastError() const`，可选。

**实现要点**：

- `controller` 优先尝试 `dynamic_cast<TinyPbRpcController*>`。
- 如果 controller 为空或不是 `TinyPbRpcController`，也允许用基础 `RpcController` 的 `SetFailed()` 表达失败。
- 如果 request 序列化失败，设置 `ERROR_FAILED_SERIALIZE`。
- 如果 response 反序列化失败，设置 `ERROR_FAILED_DESERIALIZE`。
- 如果 TinyPB 响应 `m_errCode != 0`，把服务端错误码和错误文本传给 controller。
- 该任务仍使用同步阻塞 `TcpClient`，不引入异步回调、连接池、Reactor 客户端化。

**测试与验收**：

- `test_tinypb_rpc_channel` 使用本地监听 socket 模拟服务端：
  - 验证 `CallMethod()` 写出的 TinyPB 请求包含正确 `serviceFullName` 和 request `pbData`。
  - 模拟服务端返回合法 response，验证客户端能解析到 Protobuf response。
  - 模拟服务端返回错误码，验证 controller 进入 failed 状态。
  - 模拟非法 response `pbData`，验证反序列化失败。
- 回归：
  - `./build.sh`
  - `./build/test_tinypb_rpc_channel`
  - `./build/test_tcp_client`
  - `./build/test_tinypb_dispatcher`
  - `./build/test_connection_codec`
  - `./scripts/check_stage1.sh`

**不包括**：

- 不启动真实 `TcpServer`。
- 不做 Protobuf Stub 端到端验收。
- 不实现超时、重试、连接池、异步调用。

### 任务三十九：端到端同步 RPC 示例

**为什么是下一步**：
任务三十八验证了 Channel 的内存级和 socket 级行为，但还没有证明客户端 Stub 能穿过真实 `TcpServer` 调到真实 `QueryService`。

**目标**：

- 新增一个真实服务端测试程序，注册 `QueryServiceImpl`。
- 新增一个客户端测试程序或集成测试，使用 Protobuf Stub + `TinyPbRpcChannel` 发起调用。
- 验证网络链路：Stub -> Channel -> TcpClient -> TinyPB -> TcpServer -> Dispatcher -> Service -> Response。

**关键文件**：

- `testcases/test_tinypb_server_client.cc`
- `testcases/test_tinypb_server.proto`
- `mytinyrpc/net/tcpserver.h`
- `mytinyrpc/net/tinypb/tinypbdispatcher.h`
- `scripts/check_stage8_rpc.sh`
- `docs/stage-8.md`

**实现要点**：

- 使用 `127.0.0.1:19999` 或测试中动态端口。
- 服务端启动后注册 `QueryServiceImpl`。
- 客户端构造 `TinyPbRpcChannel`，再构造生成的 Stub。
- 请求字段包含 `req_no`、`id`、`type`。
- 响应字段验证 `ret_code == 0`、`name`、`id`、`req_no`。
- 自动验收脚本负责启动服务端、等待端口、运行客户端、清理进程。

**测试与验收**：

- `./build.sh`
- `./build/test_tinypb_rpc_channel`
- `./scripts/check_stage8_rpc.sh` 输出 `[stage8] PASS`
- `./scripts/check_stage1.sh` 仍通过。

**不包括**：

- 不做连接池。
- 不做异步 Stub。
- 不做 HTTP。

### 任务四十：请求号生成与 controller 语义补齐

**目标**：

- 新增统一请求号工具。
- 让 `TinyPbRpcController` 支持 msgReq、超时时间、错误码、错误文本、local/peer 地址占位。
- `TinyPbRpcChannel` 在 controller 未指定请求号时自动生成。
- 响应请求号不匹配时判定为失败。

**关键文件**：

- `mytinyrpc/comm/msgreq.h`
- `mytinyrpc/comm/msgreq.cc`
- `mytinyrpc/net/tinypb/tinypbrpccontroller.h`
- `mytinyrpc/net/tinypb/tinypbrpccontroller.cc`
- `mytinyrpc/net/tinypb/tinypbrpcchannel.cc`
- `testcases/test_msg_req.cc`
- `testcases/test_tinypb_rpc_channel.cc`

**建议接口**：

- `std::string MsgReqUtil::genMsgNumber()`
- `void TinyPbRpcController::SetMsgReq(const std::string& msgReq)`
- `const std::string& TinyPbRpcController::MsgReq() const`
- `void TinyPbRpcController::SetError(int code, const std::string& info)`
- `int TinyPbRpcController::ErrorCode() const`
- `void TinyPbRpcController::SetTimeout(int timeoutMs)`
- `int TinyPbRpcController::Timeout() const`

**测试与验收**：

- 请求号生成连续调用不为空、不重复。
- controller `Reset()` 会清空错误和请求号。
- Channel 自动生成请求号。
- 响应 `m_msgReq` 与请求不一致时失败。

**不包括**：

- 不做真正定时器超时。
- 不做多路复用请求匹配表。

### 任务四十一：同步客户端超时与失败路径

**目标**：

- 为当前同步 `TcpClient` 加入连接超时和读写超时的最小实现。
- Channel 将 controller timeout 传递给 client。
- 失败时返回明确错误码和错误文本。

**关键文件**：

- `mytinyrpc/net/tcpclient.h`
- `mytinyrpc/net/tcpclient.cc`
- `mytinyrpc/comm/errorcode.h`
- `testcases/test_tcp_client.cc`
- `testcases/test_tinypb_rpc_channel.cc`

**实现要点**：

- 可以先采用 `setsockopt(SO_RCVTIMEO/SO_SNDTIMEO)` 或 `poll()`，避免过早引入客户端 Reactor。
- 错误码补齐：
  - `ERROR_CLIENT_CONNECT`
  - `ERROR_CLIENT_SEND`
  - `ERROR_CLIENT_RECV`
  - `ERROR_CLIENT_TIMEOUT`
  - `ERROR_RPC_MSGREQ_MISMATCH`
- 读超时、连接拒绝、服务端提前关闭都要有单独测试。

**测试与验收**：

- 模拟服务端 accept 后不返回，客户端按超时失败。
- 模拟服务端提前关闭，客户端返回接收失败。
- 模拟服务端慢响应但未超过超时，客户端成功。

**不包括**：

- 不实现异步重试。
- 不实现连接池。

## 4. 阶段九：客户端连接语义增强

### 任务四十二：`TcpClient` 重试与重连边界

**目标**：

- 为同步客户端增加有限重试次数。
- 连接失败时允许重新创建 fd。
- 成功调用后保持当前“单连接同步调用”语义，不做复用池。

**关键文件**：

- `mytinyrpc/net/tcpclient.h`
- `mytinyrpc/net/tcpclient.cc`
- `testcases/test_tcp_client.cc`
- `docs/stage-9.md`

**测试与验收**：

- 第一次连接失败、第二次连接成功的模拟场景。
- 重试次数耗尽后返回失败。
- `closeConnection()` 后再次 `sendAndRecvTinyPb()` 可重新连接。

**不包括**：

- 不做后台自动重连。
- 不做连接池。

### 任务四十三：客户端侧 TinyPB 响应缓存和请求匹配

**目标**：

- 为后续异步客户端做铺垫，先在 `TcpClient` 或 `TcpConnection` 中支持按 `msgReq` 匹配响应。
- 当前同步调用只等待目标 `msgReq`，遇到其他响应可暂存。

**关键文件**：

- `mytinyrpc/net/tcpclient.h`
- `mytinyrpc/net/tcpclient.cc`
- `mytinyrpc/net/tcpconnection.h`
- `mytinyrpc/net/tcpconnection.cc`
- `testcases/test_tcp_client.cc`

**测试与验收**：

- 服务端先返回非目标 msgReq，再返回目标 msgReq，客户端最终拿到目标响应。
- 非目标响应被缓存或被明确丢弃，行为在文档中写清。

**不包括**：

- 不做并发请求。
- 不做异步回调。

### 任务四十四：第八、九阶段文档和 RPC 回归套件

**目标**：

- 收口同步客户端 RPC。
- 补齐 `docs/stage-8.md`、`docs/stage-9.md`。
- 新增统一 RPC 回归脚本。

**关键文件**：

- `docs/stage-8.md`
- `docs/stage-9.md`
- `scripts/check_rpc_sync.sh`
- `README.md`

**验收标准**：

- `./scripts/check_rpc_sync.sh` 一次性运行 Channel、TcpClient、Dispatcher、端到端 RPC 测试。
- README 能说明如何启动同步 RPC 服务端和客户端。

## 5. 阶段十：定时器与 Reactor 能力补齐

### 任务四十五：实现 `TimerEvent` 和基础时间函数

**目标**：

- 新增 `TimerEvent`，表示一次性或重复定时任务。
- 新增 `getNowMs()`。
- 只做内存级测试，不接入 `timerfd`。

**关键文件**：

- `mytinyrpc/net/timer.h`
- `mytinyrpc/net/timer.cc`
- `testcases/test_timer_event.cc`
- `CMakeLists.txt`
- `docs/stage-10.md`

**建议接口**：

- `int64_t getNowMs()`
- `TimerEvent::TimerEvent(int64_t intervalMs, bool repeated, std::function<void()> task)`
- `void TimerEvent::resetTime()`
- `void TimerEvent::cancel()`
- `void TimerEvent::cancelRepeated()`
- `void TimerEvent::wake()`

**测试与验收**：

- 到期时间大于当前时间。
- `resetTime()` 会刷新到期时间。
- cancel 后不会执行任务。

### 任务四十六：实现 `Timer` 与 `timerfd`

**目标**：

- `Timer` 继承或组合 `FdEvent`。
- 使用 `timerfd_create()`、`timerfd_settime()` 接入 Reactor。
- 支持添加、删除、重置最近到期时间。

**关键文件**：

- `mytinyrpc/net/timer.h`
- `mytinyrpc/net/timer.cc`
- `mytinyrpc/net/reactor.h`
- `mytinyrpc/net/reactor.cc`
- `testcases/test_timer.cc`

**测试与验收**：

- 添加一次性定时任务，Reactor loop 后任务执行一次。
- 添加重复任务，执行多次后取消。
- 多个任务按到期顺序执行。
- 删除定时任务后不执行。

**不包括**：

- 不接入 TCP 连接超时。

### 任务四十七：Reactor 任务队列和 wakeup fd

**目标**：

- 允许其他线程向 Reactor 投递任务。
- 增加 wakeup fd，唤醒阻塞中的 `epoll_wait()`。
- 明确 `addTask()`、`addCoroutine()`、`wakeup()` 语义。

**关键文件**：

- `mytinyrpc/net/reactor.h`
- `mytinyrpc/net/reactor.cc`
- `testcases/test_reactor.cc`

**测试与验收**：

- loop 线程外投递任务，任务能被执行。
- 多个任务按提交顺序执行。
- `stop()` 能唤醒 loop 并退出。

### 任务四十八：连接时间轮

**目标**：

- 复刻 `TcpConnectionTimeWheel` 的简化版本。
- 用定时器定期推进 bucket。
- 连接活跃时刷新槽位，长时间不活跃时触发关闭。

**关键文件**：

- `mytinyrpc/net/tcpconnection_timewheel.h`
- `mytinyrpc/net/tcpconnection_timewheel.cc`
- `mytinyrpc/net/abstractslot.h`
- `mytinyrpc/net/tcpconnection.h`
- `mytinyrpc/net/tcpconnection.cc`
- `testcases/test_tcp_timewheel.cc`

**测试与验收**：

- slot 析构时能触发连接超时标记。
- 活跃连接刷新后不会被提前关闭。
- 超时连接被标记并由连接主循环关闭。

**不包括**：

- 不做完整多线程 IOThread。

## 6. 阶段十一：IO 线程与服务端多 Reactor

### 任务四十九：实现 `Mutex`、`RWMutex` 和基础线程工具

**目标**：

- 复刻原项目 `Mutex`、`RWMutex` 的最小版本。
- 替换临时锁或补齐后续 IOThread 所需同步原语。

**关键文件**：

- `mytinyrpc/net/mutex.h`
- `mytinyrpc/net/mutex.cc`
- `testcases/test_mutex.cc`

**测试与验收**：

- 多线程递增计数验证互斥锁。
- 读写锁基本加锁/解锁不死锁。

### 任务五十：实现 `IOThread`

**目标**：

- 每个 `IOThread` 持有一个 Sub Reactor。
- 线程启动后进入 Reactor loop。
- 支持向该线程添加一个客户端连接或任务。

**关键文件**：

- `mytinyrpc/net/iothread.h`
- `mytinyrpc/net/iothread.cc`
- `mytinyrpc/net/reactor.h`
- `testcases/test_iothread.cc`

**建议接口**：

- `IOThread::IOThread()`
- `IOThread::~IOThread()`
- `Reactor* IOThread::getReactor()`
- `void IOThread::addClient(TcpConnection* conn)`
- `void IOThread::setThreadIndex(int index)`
- `int IOThread::getThreadIndex() const`

**测试与验收**：

- 创建 IOThread 后能启动 Reactor。
- 投递任务能在 IOThread 所在线程执行。
- 析构或 stop 能退出线程。

### 任务五十一：实现 `IOThreadPool`

**目标**：

- 管理多个 IOThread。
- Round-robin 返回下一个 IOThread。
- 支持向指定线程和所有线程投递任务。

**关键文件**：

- `mytinyrpc/net/iothread.h`
- `mytinyrpc/net/iothread.cc`
- `testcases/test_iothread_pool.cc`

**测试与验收**：

- pool size 为 2 时，连续取线程轮转。
- `broadcastTask()` 每个线程执行一次。
- `addTaskByIndex()` 只在目标线程执行。

### 任务五十二：`TcpServer` 接入 IOThreadPool

**目标**：

- 主 Reactor 只负责 accept。
- 新连接分发到 IOThreadPool 的 Sub Reactor。
- `TcpConnection` 生命周期由 IOThread 管理。

**关键文件**：

- `mytinyrpc/net/tcpserver.h`
- `mytinyrpc/net/tcpserver.cc`
- `mytinyrpc/net/tcpconnection.h`
- `mytinyrpc/net/tcpconnection.cc`
- `scripts/check_stage11_server.sh`

**测试与验收**：

- 多客户端并发请求不会互相阻塞。
- 端到端 TinyPB RPC 在多 IO 线程下仍通过。
- `./scripts/check_rpc_sync.sh` 仍通过。

**不包括**：

- 不做异步客户端。
- 不做 HTTP。

## 7. 阶段十二：HTTP 协议栈

### 任务五十三：HTTP 基础数据结构

**目标**：

- 新增 HTTP method、status code、header 公共类型。
- 新增 `HttpRequest` 和 `HttpResponse`。

**关键文件**：

- `mytinyrpc/net/http/http_define.h`
- `mytinyrpc/net/http/http_define.cc`
- `mytinyrpc/net/http/http_request.h`
- `mytinyrpc/net/http/http_response.h`
- `testcases/test_http_define.cc`

**测试与验收**：

- `httpCodeToString(200)` 返回 `OK`。
- header 可设置、读取、序列化。
- response 可生成状态行、header 和 body。

### 任务五十四：HTTP 请求解码

**目标**：

- 实现 `HttpCodec::decode()`。
- 支持解析 request line、headers、body。
- 能处理半包：数据不完整时不误判成功。

**关键文件**：

- `mytinyrpc/net/http/httpcodec.h`
- `mytinyrpc/net/http/httpcodec.cc`
- `testcases/test_http_codec.cc`
- `CMakeLists.txt`

**测试与验收**：

- GET 请求解析 method/path/query/version。
- POST 请求按 `Content-Length` 读取 body。
- header 大小写策略在文档中说明。
- 半包输入第一次 decode 不成功，补齐后成功。

### 任务五十五：HTTP 响应编码

**目标**：

- 实现 `HttpCodec::encode()`。
- 将 `HttpResponse` 编码到 `TcpBuffer`。

**关键文件**：

- `mytinyrpc/net/http/httpcodec.cc`
- `testcases/test_http_codec.cc`

**测试与验收**：

- 200 response 包含状态行、Content-Type、Content-Length、body。
- 404 response 能正确编码。

### 任务五十六：HTTP Servlet 与 Dispatcher

**目标**：

- 新增 `HttpServlet` 抽象类。
- 新增 `NotFoundHttpServlet`。
- 新增 `HttpDispatcher`，根据 path 分发。

**关键文件**：

- `mytinyrpc/net/http/httpservlet.h`
- `mytinyrpc/net/http/httpservlet.cc`
- `mytinyrpc/net/http/httpdispatcher.h`
- `mytinyrpc/net/http/httpdispatcher.cc`
- `testcases/test_http_dispatcher.cc`

**测试与验收**：

- 注册 `/hello` servlet 后返回业务 body。
- 未注册路径返回 404。
- servlet 能设置 status、content-type、body。

### 任务五十七：HTTP Server 示例和验收脚本

**目标**：

- `TcpServer` 支持选择 HTTP codec + HTTP dispatcher。
- 提供最小 HTTP server 示例。
- 用 curl 或本地 socket 验证 HTTP 请求。

**关键文件**：

- `testcases/test_http_server.cc`
- `scripts/check_stage12_http.sh`
- `docs/stage-12.md`

**测试与验收**：

- `curl http://127.0.0.1:19999/hello` 返回预期 body。
- 未知路径返回 404。
- TinyPB RPC 回归仍通过。

## 8. 阶段十三：配置、日志和启动脚手架

### 任务五十八：XML 配置读取

**目标**：

- 复刻最小 `Config`。
- 读取日志、协程、IOThread、连接超时等基础配置。

**关键文件**：

- `mytinyrpc/comm/config.h`
- `mytinyrpc/comm/config.cc`
- `conf/test_tinypb_server.xml`
- `conf/test_http_server.xml`
- `testcases/test_config.cc`

**测试与验收**：

- 读取合法 XML 得到预期字段。
- 缺少字段时使用默认值。
- 非法文件路径返回明确错误或失败状态。

**不包括**：

- 不复刻 MySQL 插件，除非后续明确需要。

### 任务五十九：异步日志系统

**目标**：

- 将当前简单日志升级为接近原项目的 RPC 日志和 APP 日志。
- 支持日志级别、异步 flush、按大小滚动。

**关键文件**：

- `mytinyrpc/comm/log.h`
- `mytinyrpc/comm/log.cc`
- `testcases/test_log.cc`

**测试与验收**：

- DEBUG/INFO/WARN/ERROR 级别过滤正确。
- RPC 日志和 APP 日志写入不同文件。
- flush 后文件中能读到日志内容。
- 日志关闭时不输出。

### 任务六十：启动入口和全局运行时

**目标**：

- 新增 `InitConfig()`、`StartRpcServer()`、`GetServer()` 等启动 API。
- 新增服务注册宏。
- 新增最小 `RunTime` 保存当前请求号。

**关键文件**：

- `mytinyrpc/comm/start.h`
- `mytinyrpc/comm/start.cc`
- `mytinyrpc/comm/runtime.h`
- `testcases/test_start.cc`
- `README.md`

**测试与验收**：

- 使用配置文件启动 TinyPB server。
- `REGISTER_SERVICE(QueryServiceImpl)` 能注册服务。
- `REGISTER_HTTP_SERVLET("/hello", HelloServlet)` 能注册 HTTP servlet。

## 9. 阶段十四：协程池、内存池和 hook 补齐

### 任务六十一：协程栈内存池

**目标**：

- 新增固定块内存池，供协程栈复用。
- 支持申请、归还、引用计数或占用标记。

**关键文件**：

- `mytinyrpc/coroutine/memory.h`
- `mytinyrpc/coroutine/memory.cc`
- `testcases/test_memory_pool.cc`

**测试与验收**：

- 可申请固定数量 block。
- 归还后可再次申请到 block。
- 非本池 block 不可归还。

### 任务六十二：协程池

**目标**：

- 新增 `CoroutinePool`。
- 复用协程对象和栈内存。
- 支持 `getCoroutineInstance()` 和 `returnCoroutine()`。

**关键文件**：

- `mytinyrpc/coroutine/coroutine_pool.h`
- `mytinyrpc/coroutine/coroutine_pool.cc`
- `testcases/test_coroutine_pool.cc`

**测试与验收**：

- 从池中取出的协程可运行任务。
- 归还后可复用。
- 池耗尽时行为明确：返回空或新建，二选一并写入文档。

### 任务六十三：hook 覆盖面补齐

**目标**：

- 在现有 `read_hook/write_hook` 基础上，补齐常用 socket hook。
- 重点覆盖 `connect`、`accept`、`sleep/usleep`、`recv/send`。

**关键文件**：

- `mytinyrpc/coroutine/coroutine_hook.h`
- `mytinyrpc/coroutine/coroutine_hook.cc`
- `testcases/test_hook.cc`

**测试与验收**：

- hook 开关打开时，阻塞调用能让出协程。
- hook 关闭时，走系统调用原始行为。
- 超时路径能恢复协程并返回失败。

## 10. 阶段十五：异步 RPC Channel

### 任务六十四：异步 Channel 生命周期

**目标**：

- 新增 `TinyPbRpcAsyncChannel`。
- 保存 controller/request/response/closure 的生命周期。
- 同步复用 `TinyPbRpcChannel`，先跑通异步接口外壳。

**关键文件**：

- `mytinyrpc/net/tinypb/tinypbrpcasyncchannel.h`
- `mytinyrpc/net/tinypb/tinypbrpcasyncchannel.cc`
- `testcases/test_tinypb_rpc_async_channel.cc`

**测试与验收**：

- `saveCallee()` 后对象生命周期保持到回调执行。
- `CallMethod()` 能触发 done closure。
- controller 失败时 closure 仍可执行并观察错误。

### 任务六十五：异步 Channel 接入 IOThread 和协程

**目标**：

- 将异步调用投递到 IOThread。
- 当前协程等待，调用完成后恢复。
- 支持 `wait()` 和 `setFinished()`。

**关键文件**：

- `mytinyrpc/net/tinypb/tinypbrpcasyncchannel.cc`
- `mytinyrpc/net/iothread.h`
- `mytinyrpc/coroutine/coroutine.h`
- `testcases/test_tinypb_rpc_async_channel.cc`

**测试与验收**：

- 在协程中发起异步 RPC，等待响应后恢复。
- 多个异步调用可并发进行。
- 超时或服务端错误能恢复协程。

### 任务六十六：异步端到端示例

**目标**：

- 新增异步 TinyPB client 示例。
- 服务端仍使用真实 `QueryServiceImpl`。
- 验证多个并发请求。

**关键文件**：

- `testcases/test_tinypb_async_client.cc`
- `scripts/check_rpc_async.sh`
- `docs/stage-15.md`

**测试与验收**：

- 10 个并发异步请求全部收到正确响应。
- 一个请求失败不影响其他请求。

## 11. 阶段十六：代码生成器与模板工程

### 任务六十七：生成器输入参数和模板复制

**目标**：

- 复刻 `generator/tinyrpc_generator.py` 的最小命令行框架。
- 支持指定 proto 文件、服务名、输出目录。
- 能复制固定模板到输出目录。

**关键文件**：

- `generator/tinyrpc_generator.py`
- `generator/template/*`
- `testcases` 或 `scripts/check_generator.sh`

**测试与验收**：

- 执行生成器后产生 `conf.xml`、`main.cc`、`server.h/cc`、`run.sh`、`shutdown.sh`。
- 输出目录不存在时自动创建。

### 任务六十八：解析 proto 并生成接口骨架

**目标**：

- 根据 proto service/method 生成 interface 文件。
- 生成业务实现占位类。
- 生成客户端测试模板。

**关键文件**：

- `generator/tinyrpc_generator.py`
- `generator/template/interface.h.template`
- `generator/template/interface.cc.template`
- `generator/template/test_tinyrpc_client.cc.template`

**测试与验收**：

- 用 `test_tinypb_server.proto` 生成工程。
- 生成代码能编译。
- 生成客户端能调用 QueryService。

### 任务六十九：生成工程端到端验收

**目标**：

- 生成一个独立示例工程。
- 构建、启动、调用、关闭全自动。

**关键文件**：

- `scripts/check_generator_project.sh`
- `docs/stage-16.md`

**测试与验收**：

- 脚本输出 `[generator] PASS`。
- 生成工程 README 说明如何运行。

## 12. 阶段十七：工程收口与完整验收

### 任务七十：目录和命名对齐

**目标**：

- 对齐原项目主要目录：`comm`、`coroutine`、`net`、`net/http`、`net/tcp`、`net/tinypb`、`generator`、`conf`。
- 在不破坏现有 include 的前提下整理命名。

**关键文件**：

- `CMakeLists.txt`
- `mytinyrpc/**`
- `README.md`

**测试与验收**：

- 所有测试仍可构建。
- 旧 include 若保留兼容，应有说明。

### 任务七十一：完整文档补齐

**目标**：

- 补齐总体设计、详细设计、技术手册、工程规范。
- 每个核心模块都有“职责、关键类、数据流、测试方式、限制”。

**关键文件**：

- `docs/10-总体设计/`
- `docs/20-详细设计/`
- `docs/30-技术手册/`
- `docs/40-工程规范/`
- `README.md`

**验收标准**：

- 新人能只读 README 和技术手册跑通 TinyPB RPC、HTTP server、生成器示例。

### 任务七十二：完整回归脚本

**目标**：

- 新增一键全量验收脚本。
- 串联构建、单元测试、RPC 同步、RPC 异步、HTTP、生成器。

**关键文件**：

- `scripts/check_all.sh`
- `scripts/check_all.ps1`

**验收标准**：

- Linux/WSL 环境 `./scripts/check_all.sh` 通过。
- Windows PowerShell 环境 `.\scripts\check_all.ps1` 至少能完成构建和可运行测试。

### 任务七十三：与原 TinyRPC 功能对照表

**目标**：

- 建立复刻覆盖矩阵。
- 明确哪些功能已覆盖、哪些功能有意简化、哪些功能不复刻。

**关键文件**：

- `docs/original-coverage-matrix.md`
- `docs/future-replica-plan.md`

**验收标准**：

- 覆盖矩阵包含原项目模块：
  - `comm/config`
  - `comm/log`
  - `comm/start`
  - `coroutine`
  - `coroutine_pool`
  - `net/reactor`
  - `net/timer`
  - `net/tcp`
  - `net/http`
  - `net/tinypb`
  - `generator`

### 任务七十四：最终示例和学习总结

**目标**：

- 整理最终示例：
  - TinyPB 同步 RPC。
  - TinyPB 异步 RPC。
  - HTTP server。
  - 生成器生成的业务工程。
- 写学习总结，说明从阻塞 Echo 到完整 RPC 框架的演进路径。

**关键文件**：

- `examples/`
- `docs/learning-summary.md`
- `README.md`

**验收标准**：

- 每个示例都有构建和运行步骤。
- 总结文档能对应本文档每个阶段。

## 13. 建议执行节奏

为了“稍微快一点点，但不要太快”，建议后续节奏如下：

- 每个普通任务保持 1 到 3 个核心文件变化，测试文件可以同任务追加。
- 每 3 到 5 个任务收口一个阶段文档和一个脚本。
- 同一任务允许合并“一个小接口 + 一个最小测试 + 一个最小实现”，但不要合并跨领域内容，例如不要把 HTTP、IOThread、异步 RPC 放进同一任务。
- 每次开始新任务前仍按 `AGENTS.md` 执行：
  - 查看 `git -C D:\codeproject\cpp\rpc log --oneline -10`。
  - 读取阶段文档。
  - 必要时读实际源码确认进度。
  - 对齐 `docs/replica-progress.md` 和本文档。

## 14. 下一次任务分配建议

下一次应分配：

**任务三十八：实现最小 `TinyPbRpcChannel`。**

它足够小，直接建立在任务三十七的 `TcpClient::sendAndRecvTinyPb()` 之上；同时又是完整 RPC 客户端的关键跃迁点。完成后，复刻项目就能从“手写 TinyPbStruct 收发”进入“Protobuf Stub 可用”的阶段。
