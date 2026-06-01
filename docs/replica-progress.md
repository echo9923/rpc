# TinyRPC 复刻进度

本文记录每个任务完成后的能力增量、验证方式和当前限制，便于后续阶段回归。

## 阶段 8：同步 RPC 客户端闭环

### 任务三十八：实现最小 `TinyPbRpcChannel`

已完成能力：

- 新增 `TinyPbRpcChannel`，继承 `google::protobuf::RpcChannel`。
- Protobuf 生成的 `QueryService_Stub` 可以通过 Channel 调用 `CallMethod()`。
- Channel 会把 `MethodDescriptor::full_name()` 写入 TinyPB `serviceFullName`。
- Channel 会把 Protobuf request 序列化到 TinyPB `pbData`，并把 response `pbData` 反序列化回业务 response。
- `TinyPbRpcController` 支持记录错误码、错误文本和本次请求号。
- 新增 `test_tinypb_rpc_channel`，覆盖正常响应、服务端 TinyPB 错误、非法 response payload 和 done closure。

验证命令：

```bash
./build.sh
./build/test_tinypb_rpc_channel
./build/test_tinypb_codec
./build/test_tinypb_dispatcher
./build/test_tcp_client
```

当前限制：

- 真实 Stub 到真实 `TcpServer` 的端到端验收留到任务三十九。
- `msgReq` 自动生成工具和 mismatch 检查留到任务四十/四十六。
- 超时、重试和连接池留到后续阶段。

### 任务三十九：真实 Stub 到服务端端到端同步 RPC

已完成能力：

- 新增 `test_tinypb_server_client`，支持 `--server`、`--client`、`--probe` 三种模式。
- 服务端模式启动真实 `TcpServer`，接入 `TinyPbCodec` 和 `TinyPbDispatcher`，注册 `QueryServiceImpl`。
- 客户端模式使用 Protobuf 生成的 `QueryService_Stub` 与 `TinyPbRpcChannel` 发起真实网络 RPC。
- 新增 `scripts/check_stage8_rpc.sh`，自动启动服务端、等待端口可连接、运行 Stub 客户端并清理服务端进程。

验证命令：

```bash
./build.sh
./scripts/check_stage8_rpc.sh
./scripts/check_stage1.sh
```

当前限制：

- 只验证单客户端单请求。
- 不做超时、重试和异步 Stub。

### 任务四十：请求号与 `TinyPbRpcController` 语义补齐

已完成能力：

- 新增 `MsgReqUtil::genMsgNumber()`，生成非空、递增且进程内不重复的请求号。
- `TinyPbRpcController` 支持 `MsgReq()`、`ErrorCode()`、`ErrorText()` 和 `Timeout()` 占位。
- `TinyPbRpcChannel` 在 controller 未预设 `msgReq` 时自动生成请求号。
- `TinyPbRpcChannel` 在 controller 已预设 `msgReq` 时复用该请求号。
- Channel 收到 response 后检查 `msgReq`，不匹配时设置 `ERROR_RPC_MSGREQ_MISMATCH`。
- 新增 `test_msg_req`，并扩展 `test_tinypb_rpc_channel` 覆盖预设请求号和 mismatch。

验证命令：

```bash
./build.sh
./build/test_msg_req
./build/test_tinypb_rpc_channel
./scripts/check_stage8_rpc.sh
```

当前限制：

- `Timeout()` 仅保存数值，不驱动实际读写超时。
- 同步客户端仍不缓存乱序响应。

### 任务四十一：同步客户端超时与失败路径

已完成能力：

- `TcpClient` 新增 `setTimeout()`、`getTimeout()` 和 `getErrorCode()`。
- connect 支持非阻塞 `connect()` + `poll(POLLOUT)` 等待，失败映射为 `ERROR_TCP_CONNECT_FAILED`。
- read/write 支持 `poll(POLLIN/POLLOUT)` 等待，超时映射为 `ERROR_TCP_TIMEOUT`。
- 对端提前关闭映射为 `ERROR_TCP_RECV_FAILED`。
- Channel 会把 controller timeout 传给内部 TcpClient，并透传明确的 TcpClient 错误码。
- `test_tcp_client` 覆盖读超时、服务端提前关闭、慢响应未超时成功。
- `test_tinypb_rpc_channel` 覆盖 controller timeout 传递。

验证命令：

```bash
./build.sh
./build/test_tcp_client
./build/test_tinypb_rpc_channel
./scripts/check_stage8_rpc.sh
```

当前限制：

- 不做异步重试。
- 不做连接池。
- 不做客户端 Reactor 化。

### 任务四十二：阶段八调用链文档和同步 RPC 回归脚本

已完成能力：

- `docs/stage-8.md` 补充同步 RPC 主调用链图。
- `docs/stage-8.md` 明确阶段八暂不支持异步、连接池、多路复用和乱序响应缓存。
- 新增 `scripts/check_rpc_sync_basic.sh`，串联构建、TinyPB codec、dispatcher、TcpClient、TinyPbRpcChannel 和真实端到端同步 RPC 验收。
- README 增加阶段八同步 RPC 基础回归入口。

验证命令：

```bash
./scripts/check_rpc_sync_basic.sh
```

## 阶段 9：同步客户端连接语义收口

### 任务四十三：`TcpClient` 重连和关闭边界

已完成能力：

- `TcpClient` 新增 `setConnectRetry(retryCount, retryIntervalMs)`。
- 连接失败后关闭失败 fd，有限重试时每次重新创建 fd。
- 主动 `closeConnection()` 后，下一次 `sendAndRecvTinyPb()` 可以重新连接并完成请求。
- 重试耗尽后保留 `ERROR_TCP_CONNECT_FAILED` 和包含尝试次数的错误文本。
- `test_tcp_client` 覆盖服务端稍后启动后的重试成功、重试耗尽失败、显式 close 后再次请求成功。

验证命令：

```bash
./build.sh
./build/test_tcp_client
./scripts/check_rpc_sync_basic.sh
```

当前限制：

- 不做后台自动重连。
- 不做连接池。
- 不做多服务节点负载均衡。

### 任务四十四：同步客户端错误码矩阵

已完成能力：

- 整理 `comm/errorcode.h`，按序列化、dispatcher、channel、TcpClient 分组。
- 新增 `docs/error-code.md`，列出错误码、层级、触发场景和测试覆盖。
- 明确 controller error、TinyPB `errCode`、业务 response `ret_code` 三层含义。
- `TcpClient` 写 socket 改为 `send(..., MSG_NOSIGNAL)`，避免对端关闭时 `SIGPIPE` 终止进程。
- `test_tcp_client` 新增 send failed 覆盖 `ERROR_TCP_SEND_FAILED`。

验证命令：

```bash
./build.sh
./build/test_tcp_client
./build/test_tinypb_rpc_channel
./scripts/check_rpc_sync_basic.sh
```

### 任务四十五：同步 RPC 稳定性回归脚本

已完成能力：

- 新增 `scripts/check_rpc_sync.sh`，作为后续阶段必须运行的同步 RPC 回归入口。
- 脚本覆盖构建、TcpBuffer、AbstractCodec、TinyPB data/codec、连接编解码、Protobuf service、dispatcher、msgReq、TcpClient、TinyPbRpcChannel 和真实端到端同步 RPC。
- README 将推荐入口从基础脚本更新为 `./scripts/check_rpc_sync.sh`。
- 阶段 9 文档明确后续阶段完成前需要运行该脚本。

验证命令：

```bash
./scripts/check_rpc_sync.sh
```

### 任务四十六：推迟响应缓存，仅保留 msgReq mismatch 检查

已完成能力：

- 明确同步 RPC 只有单 in-flight request，`TinyPbRpcChannel` 收到一个 response 后立即进行 `msgReq` 校验。
- `msgReq` 不匹配时直接设置 `ERROR_RPC_MSGREQ_MISMATCH`，不反序列化业务 response。
- 同步 `TcpClient` 和 `TinyPbRpcChannel` 文档注释明确不维护 `msgReq -> response` 缓存。
- 阶段 9 文档说明 pending map、乱序响应缓存和迟到响应处理留到异步 RPC 阶段。

验证命令：

```bash
./build/test_tinypb_rpc_channel
./scripts/check_rpc_sync.sh
```

## 阶段 10：Timer、Reactor wakeup 和连接生命周期

### 任务四十七：`TimerEvent` 与基础时间函数

已完成能力：

- 新增 `mytinyrpc/net/timer.h` 和 `mytinyrpc/net/timer.cc`。
- 新增 `getNowMs()`，提供毫秒级时间基准。
- 新增 `TimerEvent`，支持一次性任务、重复任务、cancel、reset 和到期判断。
- 新增 `test_timer_event`，以内存级测试覆盖 TimerEvent 行为，不依赖 Reactor。
- 新增 `docs/stage-10.md` 记录当前边界：本任务不接入 `timerfd`，不接入 TcpConnection。

验证命令：

```bash
./build.sh
./build/test_timer_event
./scripts/check_rpc_sync.sh
```

### 任务四十八：`Timer` + `timerfd` 接入 Reactor

已完成能力：

- 新增 `Timer`，通过 `timerfd_create(CLOCK_MONOTONIC, TFD_NONBLOCK | TFD_CLOEXEC)` 把时间到期转换成 fd 可读事件。
- `Reactor` 构造时持有 `Timer`，并通过 `getTimer()` 提供添加/删除定时任务入口。
- `Timer` 使用 `FdEvent` 注册 timerfd，Reactor 可通过 `epoll_wait()` 唤醒并执行 timer callback。
- `Timer` 添加、删除、执行任务后都会按最近到期任务刷新 `timerfd_settime()`。
- `test_timer` 覆盖一次性任务、重复任务、多个任务按到期时间触发、删除任务后不触发。
- `docs/stage-10.md` 补充 timerfd 触发路径和当前边界。

验证命令：

```bash
./build.sh
./build/test_timer
./build/test_reactor
./scripts/check_rpc_sync.sh
```

### 任务四十九：Reactor 任务队列和 wakeup fd

已完成能力：

- `Reactor` 新增 `addTask()`、`loop()`、`stop()`。
- Reactor 内部使用 `eventfd(EFD_NONBLOCK | EFD_CLOEXEC)` 作为 wakeup fd，并注册到 epoll。
- 其他线程调用 `addTask()` 后会写 eventfd，唤醒阻塞中的 `epoll_wait()`。
- wakeup 回调读取 eventfd 计数并在 Reactor 线程按提交顺序执行任务队列。
- `stop()` 不依赖额外网络事件，可唤醒阻塞中的 loop 并退出。
- `test_reactor` 覆盖跨线程 addTask、任务顺序和 stop 唤醒。

验证命令：

```bash
./build.sh
./build/test_reactor
./build/test_timer
./scripts/check_rpc_sync.sh
```

### 任务五十：Reactor 安全退出和事件生命周期回归

已完成能力：

- `Reactor` 新增 `addFdEvent()` 和 `delFdEvent()`，作为 fd event 生命周期的清晰入口。
- 同一个 `FdEvent` 重复注册保持幂等，不同 `FdEvent` 重复注册同一个 fd 会被拒绝。
- `epollMod()` 和 `epollDel()` 增加 owner 检查，避免非注册对象修改或删除同一 fd。
- `test_reactor` 补充重复注册、callback 内调用 `stop()`、callback 线程归属验证。
- `docs/stage-10.md` 补充 fd event 生命周期图和 Reactor 事件生命周期边界。

验证命令：

```bash
./build.sh
./build/test_reactor
./build/test_timer
./scripts/check_rpc_sync.sh
```

### 任务五十一：连接空闲超时 / 简化时间轮

已完成能力：

- 新增 `TcpConnectionTimeWheel`，用每连接一个重复 `TimerEvent` 的方式实现简化空闲超时管理。
- `TcpConnection` 新增 `isClosed()`、`getLastActiveTimeMs()` 和 `refreshActiveTime()`，读到真实数据时刷新活跃时间。
- 活跃连接刷新后不会被误关闭。
- 空闲连接超时后会被时间轮移除，并通过连接所属 Reactor 的 task 队列执行关闭。
- `test_tcp_timewheel` 覆盖活跃刷新、空闲关闭、关闭 callback 的 Reactor 线程归属。
- 阶段 10 文档补充 TcpConnection 空闲超时路径和当前边界。

验证命令：

```bash
./build.sh
./build/test_tcp_timewheel
./build/test_reactor
./build/test_timer
./scripts/check_rpc_sync.sh
```

### 任务五十二：Reactor / Timer / TcpConnection 调试文档

已完成能力：

- 新增 `docs/reactor-event-lifecycle.md`，说明 fd event、timerfd、wakeup、stop、callback 线程归属和常见排查点。
- 新增 `docs/tcpconnection-lifetime.md`，说明 TcpConnection 创建、读写、关闭、空闲超时、fd 归属和排查清单。
- `docs/stage-10.md` 增加阶段 10 调试索引，指向独立生命周期文档。
- 文档明确：fd callback、Timer callback、wakeup task 和空闲超时关闭动作都在 Reactor 线程执行。

验证命令：

```bash
./build.sh
./build/test_reactor
./build/test_timer
./build/test_tcp_timewheel
./scripts/check_rpc_sync.sh
```

## 阶段 11：IOThread 与服务端多 Reactor

### 任务五十三：`Mutex`、`RWMutex` 和基础线程工具

已完成能力：

- 新增 `mytinyrpc/net/mutex.h` 和 `mytinyrpc/net/mutex.cc`。
- `Mutex` 封装 `std::mutex`，提供 `lock()`、`unlock()`、`tryLock()`。
- `MutexLockGuard` 提供互斥锁 RAII 使用方式。
- `RWMutex` 封装 `std::shared_mutex`，提供读锁和写锁接口。
- `ReadLockGuard` 和 `WriteLockGuard` 提供读写锁 RAII 使用方式。
- 新增 `test_mutex`，覆盖多线程互斥递增、多读并发、写锁独占。
- 新增 `docs/stage-11.md` 记录阶段 11 起点和当前边界。

验证命令：

```bash
./build.sh
./build/test_mutex
./scripts/check_rpc_sync.sh
```

### 任务五十四：`IOThread` 生命周期

已完成能力：

- 新增 `mytinyrpc/net/iothread.h` 和 `mytinyrpc/net/iothread.cc`。
- `IOThread` 内部持有一个 `Reactor`，构造时启动后台线程并进入 `Reactor::loop()`。
- 提供 `getReactor()`、`addTask()`、`stop()`、`getThreadId()` 和 `isStarted()`。
- `addTask()` 投递的任务在线程内部 Reactor 执行，线程归属可观察。
- `stop()` 可重复调用，析构时会兜底停止并 join 线程。
- 新增 `test_iothread`，覆盖线程启动、任务执行线程、stop 幂等。
- `docs/stage-11.md` 补充 IOThread 生命周期图和当前边界。

验证命令：

```bash
./build.sh
./build/test_iothread
./build/test_reactor
./scripts/check_rpc_sync.sh
```

### 任务五十五：`IOThreadPool`

已完成能力：

- 新增 `mytinyrpc/net/iothread_pool.h` 和 `mytinyrpc/net/iothread_pool.cc`。
- `IOThreadPool` 构造时启动固定数量 `IOThread`。
- `getNextIOThread()` 按 round-robin 分配线程。
- `getIOThreadByIndex()` 和 `addTaskByIndex()` 支持指定 index 获取或投递任务。
- `broadcastTask()` 向每个 IOThread 各投递一次任务。
- `stop()` 停止池内全部线程，析构时兜底调用。
- 新增 `test_iothread_pool`，覆盖轮转、广播、指定 index 投递和线程归属。
- `docs/stage-11.md` 补充 IOThreadPool 任务投递路径和当前边界。

验证命令：

```bash
./build.sh
./build/test_iothread_pool
./build/test_iothread
./scripts/check_rpc_sync.sh
```

### 任务五十六：`TcpServer` 接入 IOThreadPool

已完成能力：

- `TcpServer` 新增 `setIOThreadNum()` 和 `getIOThreadNum()`。
- `TcpServer` 启用 IOThreadPool 后，Main Reactor 只负责 accept，新连接按 round-robin 分配到 Sub Reactor。
- `TcpConnection` 使用目标 Sub Reactor 创建，并把 `startConnection()` 投递到对应 IOThread 执行。
- 单线程模式保持旧行为，不设置 IOThread 数量时仍由 Main Reactor 处理连接。
- 连接表使用 `Mutex` 保护，关闭回调可从 Sub Reactor 线程安全删除连接记录。
- `test_tinypb_server_client` 新增 `--server-multi <port> <threads>` 模式。
- 新增 `scripts/check_stage11_server.sh`，并发运行 8 个 Stub 客户端验证多 Reactor 同步 RPC。
- `docs/stage-11.md` 补充 TcpServer 多 Reactor 分发路径和当前边界。

验证命令：

```bash
./build.sh
./scripts/check_stage11_server.sh
./scripts/check_rpc_sync.sh
```

### 任务五十七：`TcpConnection` 所有权和状态机文档

已完成能力：

- 完善 `docs/tcpconnection-lifetime.md`，覆盖单 Reactor 和多 Reactor 两种连接生命周期。
- 明确 `TcpConnection` 对象由 `TcpServer::m_connections` 主要持有，读协程和 IOThread task 通过捕获 `shared_ptr` 临时保活。
- 明确 fd、`FdEvent`、input buffer、output buffer、codec 和 dispatcher 的所有权关系。
- 明确 Main Reactor 负责 accept，多 Reactor 模式下连接注册、读写、dispatcher 和关闭动作都归属连接所在 Sub Reactor。
- `docs/stage-11.md` 补充 TcpConnection 线程归属速查表和当前边界。

验证命令：
```bash
./scripts/check_stage11_server.sh
./scripts/check_rpc_sync.sh
```

## 阶段 12：HTTP 协议栈

### 任务五十八：HTTP 基础数据结构

已完成能力：

- 新增 `mytinyrpc/net/http/http_define.h` 和 `http_define.cc`，定义 HTTP method、status code、header 类型和基础转换函数。
- 新增 `HttpRequest`，支持 method、path、version、header 和 body 的设置与读取。
- 新增 `HttpResponse`，支持 status、version、header 和 body 的设置与读取，并可生成最小 HTTP response 字符串。
- 新增 `test_http_define`，覆盖 `httpCodeToString(200)`、header 设置读取和 response 状态行、header、body 生成。
- 新增 `docs/stage-12.md`，记录阶段 12 当前能力和边界。

验证命令：
```bash
./build.sh
./build/test_http_define
./scripts/check_rpc_sync.sh
```

### 任务五十九：HTTP 请求解码

已完成能力：

- 新增 `mytinyrpc/net/http/httpcodec.h` 和 `httpcodec.cc`。
- `HttpCodec::decode()` 支持解析 request line、headers 和 `Content-Length` body。
- GET 请求、POST 请求、半包补齐和非法 request line 均有 `test_http_codec` 覆盖。
- 半包路径不消费 `TcpBuffer`，补齐 body 后可继续解析成功。
- 非法 request line 路径返回失败并消费坏包，避免对同一非法输入死循环。
- `HttpCodec::encode()` 暂保持安全失败，任务六十再实现响应编码。

验证命令：
```bash
./build.sh
./build/test_http_codec
./scripts/check_rpc_sync.sh
```

### 任务六十：HTTP 响应编码

已完成能力：

- 实现 `HttpCodec::encode()`，将 `HttpResponse` 编码到 `TcpBuffer`。
- encode 前自动按 body 实际大小写入 `Content-Length`，旧的错误长度会被覆盖。
- `test_http_codec` 补充 200 response、404 response 和 `Content-Length` 修正测试。
- `docs/stage-12.md` 补充 HTTP encode 能力和当前边界。

验证命令：
```bash
./build.sh
./build/test_http_codec
./scripts/check_rpc_sync.sh
```

### 任务六十一：HttpServlet 与 HttpDispatcher

已完成能力：

- 新增 `HttpServlet` 抽象类，业务处理统一通过 `handle(HttpRequest*, HttpResponse*)` 完成。
- 新增 `NotFoundHttpServlet`，未知 path 返回 404 响应。
- 新增 `HttpDispatcher`，支持按 path 注册和分发 servlet。
- `HttpDispatcher` 保持 `AbstractDispatcher` 接口兼容，为后续接入 `TcpServer` 做准备。
- 新增 `test_http_dispatcher`，覆盖 `/hello` 路由、未知 path 和重复注册。
- `docs/stage-12.md` 补充 dispatcher 能力和当前边界。

验证命令：
```bash
./build.sh
./build/test_http_dispatcher
./scripts/check_rpc_sync.sh
```

### 任务六十二：HTTP Server 集成和脚本

已完成能力：

- 新增 `testcases/test_http_server.cc`，启动 `TcpServer` + `HttpCodec` + `HttpDispatcher`。
- 新增 `scripts/check_stage12_http.sh`，使用 curl 验证 `/hello` 返回 `hello http`，未知 path 返回 404。
- `TcpConnection::execute()` 按 codec 的 `ProtocolType` 创建对应协议数据对象，使 HTTP 和 TinyPB 共用同一套 server/connection 抽象。
- HTTP 验收脚本增加 curl 超时，避免失败路径长时间挂住。
- `docs/stage-12.md` 补充 HTTP server 集成路径和当前边界。

验证命令：
```bash
./build.sh
./scripts/check_stage12_http.sh
./scripts/check_rpc_sync.sh
```

## 阶段 13：配置、日志、启动入口和运行时

### 任务六十三：最小 Config 默认值整理

已完成能力：

- 新增 `mytinyrpc/comm/config.h` 和 `config.cc`。
- `Config` 提供默认 server host、server port、protocol、IOThread 数量、timeout 和 log level。
- 默认值写入 `docs/stage-13.md`。
- 新增 `test_config`，覆盖默认字段，并验证默认配置可用于初始化测试 server。

验证命令：
```bash
./build.sh
./build/test_config
./scripts/check_rpc_sync.sh
```

### 任务六十四：XML 配置读取

已完成能力：

- `Config` 支持通过 `loadFromXml()` 读取 XML 配置文件。
- 支持读取 server addr、protocol、iothread_num、timeout 和 log level。
- 缺失字段继续使用默认配置。
- 非法路径和非法字段类型返回失败，并通过 `getLastError()` 提供错误文本。
- 新增 TinyPB 与 HTTP XML 样例配置。
- `test_config` 验证 TinyPB server 和 HTTP server 都可以根据 XML 配置初始化。

验证命令：
```bash
./build.sh
./build/test_config
./scripts/check_rpc_sync.sh
```

### 任务六十五：日志系统分步实现

已完成能力：

- `Logger` 支持同步文件日志初始化、级别过滤和 flush。
- 日志格式包含时间、级别、线程 id、文件行号和正文。
- 支持带 `msgReq` 的日志接口，便于记录请求号、方法名和错误码。
- 支持关闭日志输出。
- 支持简化异步队列模式，`flush()` 和 `shutdown()` 会等待日志落盘。
- 未初始化文件日志时保持控制台输出，兼容现有调试路径。
- 新增 `test_log`，覆盖级别过滤、文件输出、flush、关闭日志和异步落盘。

验证命令：
```bash
./build.sh
./build/test_log
./scripts/check_rpc_sync.sh
```

### 任务六十六：启动入口和服务注册宏

已完成能力：

- 新增 `Runtime`，保存启动期配置、codec、dispatcher 和 `TcpServer`。
- 新增 `InitConfig(path)` 读取 XML 配置。
- 新增 `StartRpcServer()`，按 `protocol` 创建 TinyPB 或 HTTP server 并完成初始化。
- 新增 `GetServer()`，调用方可在注册完成后执行 `GetServer()->start()`。
- 新增 `REGISTER_SERVICE(ServiceType)` 和 `REGISTER_HTTP_SERVLET(path, ServletType)`。
- 新增 `test_start`，覆盖 XML 启动 TinyPB/HTTP server、服务注册宏和 HTTP servlet 注册宏。

验证命令：
```bash
./build.sh
./build/test_start
./scripts/check_rpc_sync.sh
```

### 任务六十七：运行时 request context

已完成能力：

- `Runtime` 新增线程局部 `RequestContext`。
- 请求上下文保存当前 msgReq、method name、local addr 和 peer addr。
- `TinyPbDispatcher` 在调用业务 Service 前设置上下文，请求结束后自动清理。
- `Logger` 未显式传 msgReq 时会读取当前线程 request context。
- 新增 `test_runtime`，覆盖请求处理中读取 msgReq、请求后清理、多线程隔离和日志自动打印 msgReq。

验证命令：
```bash
./build.sh
./build/test_runtime
./scripts/check_rpc_sync.sh
```

## 阶段 14：协程、hook、协程池和内存池整理

### 任务六十八：协程现状梳理和调试文档

已完成能力：

- 新增 `docs/coroutine-model.md`。
- 梳理 `Coroutine` 创建、状态转换、`Yield()` 和 `resume()` 路径。
- 梳理 `read_hook()` / `write_hook()` 的启用边界和主协程直通路径。
- 梳理 `FdEvent` 挂载协程、Reactor 事件匹配和恢复协程的链路。
- 补充 `TcpConnection` 中 input、execute、output 与 hook 的关系。
- 补充协程和 hook 调试清单。

验证命令：
```bash
./build.sh
./build/test_coroutine
./build/test_hook
./scripts/check_rpc_sync.sh
```
