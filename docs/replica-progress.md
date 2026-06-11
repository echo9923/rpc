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
- `reqId` 自动生成工具和 mismatch 检查留到任务四十/四十六。
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

- 新增 `ReqIdUtil::genReqId()`，生成非空、递增且进程内不重复的请求号。
- `TinyPbRpcController` 支持 `ReqId()`、`ErrorCode()`、`ErrorText()` 和 `Timeout()` 占位。
- `TinyPbRpcChannel` 在 controller 未预设 `reqId` 时自动生成请求号。
- `TinyPbRpcChannel` 在 controller 已预设 `reqId` 时复用该请求号。
- Channel 收到 response 后检查 `reqId`，不匹配时设置 `ERROR_RPC_REQID_MISMATCH`。
- 新增 `test_req_id`，并扩展 `test_tinypb_rpc_channel` 覆盖预设请求号和 mismatch。

验证命令：

```bash
./build.sh
./build/test_req_id
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
- 脚本覆盖构建、TcpBuffer、AbstractCodec、TinyPB data/codec、连接编解码、Protobuf service、dispatcher、reqId、TcpClient、TinyPbRpcChannel 和真实端到端同步 RPC。
- README 将推荐入口从基础脚本更新为 `./scripts/check_rpc_sync.sh`。
- 阶段 9 文档明确后续阶段完成前需要运行该脚本。

验证命令：

```bash
./scripts/check_rpc_sync.sh
```

### 任务四十六：推迟响应缓存，仅保留 reqId mismatch 检查

已完成能力：

- 明确同步 RPC 只有单 in-flight request，`TinyPbRpcChannel` 收到一个 response 后立即进行 `reqId` 校验。
- `reqId` 不匹配时直接设置 `ERROR_RPC_REQID_MISMATCH`，不反序列化业务 response。
- 同步 `TcpClient` 和 `TinyPbRpcChannel` 文档注释明确不维护 `reqId -> response` 缓存。
- 阶段 9 文档说明 pending map、乱序响应缓存和迟到响应处理留到异步 RPC 阶段。

验证命令：

```bash
./build/test_tinypb_rpc_channel
./scripts/check_rpc_sync.sh
```

## 阶段 10：Timer、Reactor wakeup 和连接生命周期

### 任务四十七：`TimerTask` 与基础时间函数

已完成能力：

- 新增 `mytinyrpc/net/timer.h` 和 `mytinyrpc/net/timer.cc`。
- 新增 `getNowMs()`，提供毫秒级时间基准。
- 新增 `TimerTask`，支持一次性任务、重复任务、cancel、reset 和到期判断。
- 新增 `test_timer_task`，以内存级测试覆盖 TimerTask 行为，不依赖 Reactor。
- 新增 `docs/stage-10.md` 记录当前边界：本任务不接入 `timerfd`，不接入 TcpConnection。

验证命令：

```bash
./build.sh
./build/test_timer_task
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

- 新增 `TcpConnectionTimeWheel`，用每连接一个重复 `TimerTask` 的方式实现简化空闲超时管理。
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

- 新增 `mytinyrpc/net/iothreadpool.h` 和 `mytinyrpc/net/iothreadpool.cc`。
- `IOThreadPool` 构造时启动固定数量 `IOThread`。
- `getNextIOThread()` 按 round-robin 分配线程。
- `getIOThreadByIndex()` 和 `addTaskByIndex()` 支持指定 index 获取或投递任务。
- `broadcastTask()` 向每个 IOThread 各投递一次任务。
- `stop()` 停止池内全部线程，析构时兜底调用。
- 新增 `test_iothreadpool`，覆盖轮转、广播、指定 index 投递和线程归属。
- `docs/stage-11.md` 补充 IOThreadPool 任务投递路径和当前边界。

验证命令：

```bash
./build.sh
./build/test_iothreadpool
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

- 新增 `mytinyrpc/net/http/httpdefine.h` 和 `httpdefine.cc`，定义 HTTP method、status code、header 类型和基础转换函数。
- 新增 `HttpRequest`，支持 method、path、version、header 和 body 的设置与读取。
- 新增 `HttpResponse`，支持 status、version、header 和 body 的设置与读取，并可生成最小 HTTP response 字符串。
- 新增 `test_httpdefine`，覆盖 `httpCodeToString(200)`、header 设置读取和 response 状态行、header、body 生成。
- 新增 `docs/stage-12.md`，记录阶段 12 当前能力和边界。

验证命令：
```bash
./build.sh
./build/test_httpdefine
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
- 支持带 `reqId` 的日志接口，便于记录请求号、方法名和错误码。
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
- 请求上下文保存当前 reqId、method name、local addr 和 peer addr。
- `TinyPbDispatcher` 在调用业务 Service 前设置上下文，请求结束后自动清理。
- `Logger` 未显式传 reqId 时会读取当前线程 request context。
- 新增 `test_runtime`，覆盖请求处理中读取 reqId、请求后清理、多线程隔离和日志自动打印 reqId。

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
- 梳理 `readHook()` / `writeHook()` 的启用边界和主协程直通路径。
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

### 任务六十九：`connect` hook

已完成能力：

- 新增 `connectHook()`，主协程中直通原始 `connect()`。
- 非主协程中，非阻塞 connect 返回 `EINPROGRESS` 后会挂载当前协程并等待 `EPOLLOUT`。
- Reactor 监听到可写事件后恢复协程，`connectHook()` 通过 `getsockopt(SO_ERROR)` 判断连接成功或失败。
- 支持 timeout：到期后 TimerTask 恢复协程，并返回 `ETIMEDOUT`。
- 扩展 `test_hook`，覆盖连接成功、连接拒绝、连接超时和主协程直通路径。
- 更新 `docs/coroutine-model.md`，补充 connect hook 的恢复和超时路径。

验证命令：
```bash
./build.sh
./build/test_hook
./scripts/check_rpc_sync.sh
```

### 任务七十：`sleep/usleep` hook

已完成能力：

- 新增 `sleepHook(Reactor*, seconds)`，主协程中直通原始 `sleep()`。
- 新增 `usleepHook(Reactor*, usec)`，主协程中直通原始 `usleep()`。
- 非主协程中通过一次性 `TimerTask` 挂起当前协程，Timer 到期后由 Reactor 恢复。
- 新增 `test_hook_sleep`，覆盖主协程直通、`sleepHook`/`usleepHook` 定时恢复、一个协程 sleep 不阻塞另一个协程，以及多个协程按时间恢复。
- 更新 `docs/coroutine-model.md`，补充 sleep/usleep hook 的 Reactor Timer 恢复路径和调试要点。

验证命令：
```bash
./build.sh
./build/test_hook_sleep
./build/test_hook
./scripts/check_rpc_sync.sh
```

### 任务七十一：`recv/send/accept` hook 补齐

已完成能力：

- 新增 `recvHook()`，在非主协程中遇到 `EAGAIN` / `EWOULDBLOCK` / `EINTR` 时等待 `EPOLLIN`。
- 新增 `sendHook()`，在非主协程中遇到发送缓冲区暂不可写时等待 `EPOLLOUT`。
- 新增 `acceptHook()`，在非主协程中遇到监听 socket 暂无连接时等待 `EPOLLIN`。
- 三个 socket hook 均支持可选 `timeoutMs`，超时后恢复协程并返回 `errno = ETIMEDOUT`。
- 新增 `test_hook_socket`，覆盖 recv 数据到达恢复、recv 超时、send peer drain 后恢复、accept 新连接恢复、accept 超时以及主协程直通路径。
- 更新 `docs/coroutine-model.md`，补充 socket hook 的恢复路径和当前限制。

验证命令：
```bash
./build.sh
./build/test_hook_socket
./build/test_hook
./build/test_hook_sleep
./scripts/check_rpc_sync.sh
```

当前限制：

- 不覆盖所有 `recv(2)` / `send(2)` flags 组合。
- 不做极限压测。
- 仍然使用显式 `FdEvent*` 传入方式，不做 libc 符号级全局 hook。

### 任务七十二：CoroutinePool

已完成能力：

- 新增 `Coroutine::reset()`，支持 Finished / Ready 协程重新设置入口回调并重新初始化上下文。
- 新增 `CoroutinePool`，固定容量复用协程对象和栈空间。
- `getCoroutine(cb)` 优先复用空闲协程；容量耗尽时返回 `nullptr`，不隐式扩容。
- `returnCoroutine(co)` 只接受 Ready / Finished 协程，拒绝归还 Suspended / Running 协程。
- 归还成功时会把协程 reset 成空任务，避免旧回调捕获污染下一次使用。
- 新增 `test_coroutinepool`，覆盖任务运行、复用、耗尽、挂起状态拒绝归还和 `Coroutine::reset()` 边界。

验证命令：
```bash
./build.sh
./build/test_coroutinepool
./build/test_coroutine
./build/test_hook_socket
./scripts/check_rpc_sync.sh
```

当前限制：

- 不做复杂调度器。
- 不做 work stealing。
- 不支持跨线程迁移协程。

### 任务七十三：协程栈内存池

已完成能力：

- 新增 `FixedMemoryPool`，构造时预分配固定数量固定大小 block。
- `allocate()` 支持从空闲列表借出 block，池耗尽时返回 `nullptr`。
- `deallocate()` 只允许归还本池 block，并拒绝 `nullptr`、外部指针和重复归还。
- `owns()` 支持判断指针归属。
- 新增 `test_memory_pool`，覆盖容量耗尽、归还复用、拒绝非法归还、归属判断和 block 写入。
- 更新 `docs/coroutine-model.md`，明确内存池当前是独立基础能力。

验证命令：
```bash
./build.sh
./build/test_memory_pool
./build/test_coroutinepool
./build/test_coroutine
./scripts/check_rpc_sync.sh
```

当前限制：

- `Coroutine` 仍使用 `malloc/free` 管理独立栈，暂不强制接入内存池。
- 不做复杂 slab 分配器。
- 不让内存池阻塞异步 RPC 后续任务。

## 阶段 15：异步 RPC Channel

### 任务七十四：异步 Channel 生命周期外壳

已完成能力：

- 新增 `TinyPbRpcAsyncChannel`，继承 `google::protobuf::RpcChannel`。
- 新增 `AsyncCallContext`，保存 `reqId`、method 全名、controller、request、response 和 closure。
- 异步 Channel 在参数合法时生成或复用 controller 中的 `reqId`。
- 当前外壳内部临时复用 `TinyPbRpcChannel` 完成同步 TinyPB 网络请求。
- 成功路径和失败路径都会执行 `done` closure。
- 新增 `test_tinypb_rpc_async_channel`，覆盖成功调用、网络失败仍执行 done、非法参数仍执行 done。
- 新增 `docs/stage-15.md`，记录异步阶段起点、调用链和当前边界。

验证命令：
```bash
./build.sh
./build/test_tinypb_rpc_async_channel
./build/test_tinypb_rpc_channel
./scripts/check_rpc_sync.sh
```

当前限制：

- 当前还不是真正并发异步网络 IO。
- 当前不维护 pending map。
- 当前不支持乱序响应匹配。
- 当前不做异步超时和取消。

### 任务七十五：异步请求表和 reqId 匹配

已完成能力：

- `TinyPbRpcAsyncChannel` 新增 `reqId -> AsyncCallContext` pending 表。
- `CallMethod()` 在参数合法且 request 序列化成功后先注册 pending。
- 新增 `setSyncFallbackEnabled(false)` 测试入口，用于只注册 pending、不走同步网络 fallback。
- 新增 `handleTinyPbResponse()`，按 response `reqId` 命中上下文、移除 pending、反序列化业务 response 并执行 closure。
- 支持乱序响应匹配，未知 `reqId` response 会返回 false 并保留已有 pending。
- TinyPB 错误响应会设置 controller error 并执行 closure。
- 扩展 `test_tinypb_rpc_async_channel`，覆盖乱序响应、未知 reqId 和错误响应。
- 更新 `docs/stage-15.md`，补充 pending map 调用链和当前边界。

验证命令：
```bash
./build.sh
./build/test_tinypb_rpc_async_channel
./build/test_tinypb_rpc_channel
./scripts/check_rpc_sync.sh
```

当前限制：

- pending map 只在异步 Channel 内部维护，不放回同步 `TcpClient`。
- 当前 response 到达通过测试钩子模拟，真实 IOThread/Reactor 读取留到任务七十六。
- 当前不做异步超时和取消。

### 任务七十六：异步 Channel 接入 IOThread/Reactor

已完成能力：

- `TinyPbRpcAsyncChannel` 构造时持有一个 `IOThread`。
- 默认 `CallMethod()` 在注册 pending 后把网络请求投递到 IOThread，调用线程不再阻塞等待网络返回。
- IOThread 内部执行当前最小网络路径：使用 `TcpClient::sendAndRecvTinyPb()` 连接、发送请求并读取响应。
- response 返回后由 IOThread 调用 `handleTinyPbResponse()`，按 `reqId` 完成上下文并执行 closure。
- 网络失败时会删除 pending、设置 controller error 并执行 closure，单个失败不会影响其他请求。
- 新增 `stop()`、`isIOThreadStarted()` 和 `getIOThreadId()`，用于观察和停止内部 IOThread。
- 扩展 `test_tinypb_rpc_async_channel`，覆盖 closure 执行线程归属和 10 个异步请求全部完成。
- 更新 `docs/stage-15.md`，补充 IOThread 调用链和当前边界。

验证命令：
```bash
./build.sh
./build/test_tinypb_rpc_async_channel
./build/test_iothread
./scripts/check_rpc_sync.sh
```

当前限制：

- 当前异步网络最小路径仍复用同步 `TcpClient::sendAndRecvTinyPb()`，只是执行线程已切到 IOThread。
- 不做复杂连接池策略。
- 不做自动负载均衡。
- 不做异步超时和取消。

### 任务七十七：异步超时和取消

已完成能力：

- `AsyncCallContext` 新增 `timeoutTask`，保存本次异步请求对应的一次性 `TimerTask`。
- `TinyPbRpcAsyncChannel` 读取 `TinyPbRpcController::Timeout()`，在 pending 请求注册后把超时事件投递到内部 IOThread 的 Reactor Timer。
- 超时到期后按 `reqId` 从 pending map 取出上下文，设置 `ERROR_RPC_ASYNC_TIMEOUT` 并执行 closure。
- `handleTinyPbResponse()`、网络失败、超时和取消统一通过 pending map 做一次性完成仲裁，避免二次回调。
- 请求成功、失败、超时或取消完成后都会取消对应 `TimerTask`，并清理 controller 上的内部取消回调。
- `TinyPbRpcController::StartCancel()` 支持触发 Channel 注册的取消回调；Channel 会删除 pending、设置 `ERROR_RPC_ASYNC_CANCELED` 并执行 closure。
- `TimerTask` 的取消标记改为 atomic，并显式禁止拷贝，避免跨线程取消标记的数据竞争和误拷贝。
- 扩展 `test_tinypb_rpc_async_channel`，覆盖超时清理、迟到响应不二次回调、controller 取消清理。
- 扩展 `test_req_id`，覆盖 controller 取消回调执行与 Reset 清理。
- 更新 `docs/stage-15.md`，补充超时/取消调用链和当前边界。

验证命令：
```bash
./build.sh
./build/test_tinypb_rpc_async_channel
./build/test_req_id
./build/test_timer
./build/test_timer_task
./scripts/check_rpc_sync.sh
```

当前限制：

- 当前真实网络路径仍复用同步 `TcpClient::sendAndRecvTinyPb()`，IOThread 执行同步 socket 调用期间 Reactor Timer 不能抢占正在执行的调用。
- 真实网络读写超时仍由 `TcpClient` timeout 保底，并映射为异步 RPC 超时错误。
- 当前取消只清理 Channel pending 并防止二次回调，不主动打断已经在 IOThread 内执行的同步 socket 调用。

### 任务七十八：异步 RPC 调用链文档和回归脚本

已完成能力：

- 新增 `test_tinypb_async_client`，作为脚本式异步客户端验收程序。
- `test_tinypb_async_client` 内部启动本地 TinyPB mock server，发起 6 个异步 Stub 请求，覆盖成功响应和服务端 TinyPB 错误响应。
- `test_tinypb_async_client` 单独覆盖超时请求，验证 callback 执行、controller 错误码和 pending 清理。
- 新增 `scripts/check_rpc_async.sh`，作为阶段 15 一键异步回归入口。
- `check_rpc_async.sh` 串联构建、`test_tinypb_rpc_async_channel`、`test_tinypb_async_client`、`test_req_id`、`test_timer`、`test_timer_task`、`test_tinypb_rpc_channel` 和同步 RPC 安全网。
- 更新 `docs/stage-15.md`，补充完整异步生命周期图，以及 request 发出、pending 注册、响应匹配、timeout、取消和 closure 执行线程说明。
- `CMakeLists.txt` 新增 `test_tinypb_async_client` 目标。

验证命令：
```bash
./build.sh
./build/test_tinypb_async_client
./scripts/check_rpc_async.sh
./scripts/check_rpc_sync.sh
```

当前限制：

- 异步客户端验收程序使用本地 mock TinyPB server，不启动完整 `TcpServer`。
- `check_rpc_async.sh` 会运行同步 RPC 安全网，因此耗时接近一次完整阶段回归。
- 当前不写性能报告。

## 阶段 16：代码生成器与示例工程

### 任务七十九：生成器 CLI 和模板复制

已完成能力：

- 新增 `generator/tinyrpc_generator.py`，支持 `--proto`、`--service` 和 `--out` 参数。
- 参数错误会输出 `[generator] FAIL: ...` 并返回非零退出码。
- 输出目录不存在时自动创建。
- 新增 `generator/template/` 固定模板，包含 `conf.xml`、`main.cc`、`server.h`、`server.cc`、`client.cc`、`run.sh` 和 `shutdown.sh`。
- 生成器会复制 proto 文件到输出目录，并替换模板中的服务名和 proto 文件名占位符。
- 新增 `scripts/check_generator.sh`，验证生成文件清单、关键占位符替换和非法 proto 参数错误提示。
- 新增 `docs/stage-16.md`，记录生成器阶段入口、调用链和当前边界。
- 验收过程中为 WSL 安装了 `python3`，后续可直接运行 Python 生成器。

验证命令：
```bash
./scripts/check_generator.sh
./build.sh
./scripts/check_rpc_sync.sh
```

当前限制：

- 当前只做固定模板复制，不解析 proto service/method。
- 当前模板是最小骨架，不构建独立业务工程。
- 当前不做多语言生成。

### 任务八十：proto service/method 骨架生成

已完成能力：

- `tinyrpc_generator.py` 会解析简单 proto 中指定 service 的一元 rpc method。
- 新增 `RpcMethod` 描述结构，并生成方法声明、方法实现和客户端 Stub 调用占位代码。
- 新增 `interface.h.template` 和 `interface.cc.template`，生成继承 Protobuf service 的业务实现占位类。
- `server.h.template` 持有生成的 service 实现对象，为后续注册到运行时做准备。
- `client.cc.template` 生成 `call<Service>()`，为每个 rpc method 创建 request/response 并调用 `<Service>_Stub`。
- `scripts/check_generator.sh` 验证生成文件清单、service/method 签名、非法 service 错误提示，并通过 `protoc` + `g++` 编译生成 proto、接口骨架和客户端骨架。

验证命令：
```bash
./scripts/check_generator.sh
./build.sh
./scripts/check_rpc_sync.sh
```

当前限制：

- 当前只支持简单 service block 和一元 rpc method，不做完整 Protobuf parser。
- 当前验收只编译生成骨架，不启动独立生成工程。
- 生成工程端到端运行留到任务八十一。

### 任务八十一：生成工程端到端验收

已完成能力：

- 新增 `CMakeLists.txt.template`，生成工程可通过 `MYTINYRPC_ROOT` 复用当前 MyTinyRPC 源码并构建独立 server/client。
- 新增 `README.md.template`，说明生成工程的构建、运行和关闭方式。
- `main.cc.template` 接入 `InitConfig()`、`StartRpcServer()` 和 `REGISTER_SERVICE()`，生成 server 可启动真实 TinyPB 服务。
- `client.cc.template` 接入 `TinyPbRpcChannel`，生成 client 可通过 Protobuf Stub 发起调用。
- `run.sh.template` 支持后台启动 server、写入 pid 文件并等待端口就绪。
- `shutdown.sh.template` 支持根据 pid 文件关闭生成 server。
- 新增 `scripts/check_generator_project.sh`，自动完成生成、CMake 配置、构建、启动、客户端调用和关闭。

验证命令：
```bash
./scripts/check_generator_project.sh
./scripts/check_generator.sh
./build.sh
./scripts/check_rpc_sync.sh
```

当前限制：

- 生成工程依赖本地 MyTinyRPC 源码路径，不是完全独立源码包。
- 业务方法实现仍是占位逻辑，默认返回空 proto3 response。
- 该任务完成时，生成 server 的关闭由脚本 pid 管理，框架层暂未提供 `TcpServer::stop()`；阶段 26 已补齐框架层停止入口。

## 阶段 17：工程收口、覆盖矩阵和最终文档

### 任务八十二：目录和命名兼容整理

已完成能力：

- 新增 `docs/project-structure.md`，说明当前顶层目录、核心模块、脚本入口和命名边界。
- README 增加当前结构和推荐回归入口，避免只停留在早期 Stage 1/2 状态。
- 将早期遗留测试文件 `testcases/testtcpchoserver.cc` 整理为 `testcases/test_tcp_echo_server.cc`。
- 更新 `CMakeLists.txt`、`docs/stage-1.md` 和编码规范示例，保持 `test_*.cc` 测试命名一致。
- 明确保留 `mytinyrpc` 目录名和既有 include 风格，不做大规模 API 迁移。

验证命令：
```bash
./build.sh
./scripts/check_stage1.sh
./scripts/check_rpc_sync.sh
```

当前限制：

- 当前只做小范围命名和文档整理，不批量移动 `comm`、`net`、`http`、`tinypb` 等成熟目录。
- 工作区存在若干未跟踪文件，本任务不清理用户侧未跟踪文件。

### 任务八十三：原 TinyRPC 功能覆盖矩阵

已完成能力：

- 新增 `docs/original-coverage-matrix.md`。
- 按 `comm/config`、`comm/log`、`comm/start`、`comm/runtime`、`coroutine`、`coroutinepool`、`net/reactor`、`net/timer`、`net/tcp`、`net/http`、`net/tinypb` 和 `generator` 建立覆盖矩阵。
- 每个模块标注“已复刻 / 简化复刻 / 暂不复刻”状态。
- 每个已覆盖模块都写明当前能力、简化边界和验证方式。
- 单独列出 MySQL 插件、连接池、HTTPS/HTTP2、tracing、压测优化和完整 Protobuf parser 等暂不复刻项。

验证命令：
```bash
./build.sh
./scripts/check_rpc_sync.sh
```

当前限制：

- 覆盖矩阵基于当前项目目录、阶段文档和脚本验收，不追求与原 TinyRPC 100% 行为一致。
- 任务八十四会继续把矩阵中的核心验证入口串成一键全量回归脚本。

### 任务八十四：一键全量回归

已完成能力：

- 新增 `scripts/check_all.sh`，作为 Linux/WSL 一键全量回归入口。
- 新增 `scripts/check_all.ps1`，Windows PowerShell 下通过 WSL 调用同一个 Linux 脚本。
- 全量脚本覆盖构建、Reactor/Timer/TcpConnection、IOThread/IOThreadPool、配置/日志/启动/runtime、HTTP、协程/hook、同步 RPC、多 Reactor server、异步 RPC 和生成器工程。
- README 增加 `check_all.sh` 和 `check_all.ps1` 入口。

验证命令：
```bash
./scripts/check_all.sh
```

当前限制：

- `check_all.sh` 会嵌套调用现有阶段脚本，部分构建和同步 RPC 回归会重复执行，因此耗时较长。
- Windows PowerShell 脚本只作为 WSL 包装入口，不在 Windows 本地编译 Linux 目标。

### 任务八十五：最终示例和学习总结

已完成能力：

- 新增 `examples/tinypb_sync/README.md`，说明同步 TinyPB RPC 示例运行方式和源码入口。
- 新增 `examples/tinypb_async/README.md`，说明异步 TinyPB RPC 示例运行方式、pending/timeout/cancel 覆盖点和边界。
- 新增 `examples/http_server/README.md`，说明 HTTP server 示例运行方式和源码入口。
- 新增 `examples/generated_project/README.md`，说明生成工程示例、手动生成命令和边界。
- 新增 `docs/learning-summary.md`，按阶段总结从阻塞 Echo 到 RPC 框架的演进路径、当前可展示能力、与原 TinyRPC 的关系和后续建议。
- README 重写为当前项目入口，指向 examples、覆盖矩阵、学习总结和全量回归。

验证命令：
```bash
./scripts/check_all.sh
```

当前限制：

- examples 目录以 README 型运行示例为主，复用已验证的测试程序和脚本，不复制一套新的示例源码。
- 当前总结是学习型收口文档，不是商业级用户手册。

## 阶段 18：配置、日志、启动入口和运行时补全

### 任务八十六：扩展 Config schema 到当前项目核心字段

已完成能力：

- `Config` 新增日志、协程、RPC、网络连接超时和时间轮字段。
- 所有新增字段都有明确默认值和 getter。
- `getLogLevel()` 作为兼容入口返回 RPC 日志级别。
- 数字字段严格解析，非数字、尾部脏字符和越界都会失败并记录 `getLastError()`。
- 日志级别只支持 `debug`、`info`、`warn`、`error`，大小写不敏感。

验证命令：
```bash
docker exec rpc-ubuntu bash -c "cd /workspace && rm -rf build && bash build.sh && ./build/test_config"
docker exec rpc-ubuntu bash -c "cd /workspace && ./scripts/check_all.sh"
```

当前限制：

- 该任务只补齐内存字段和解析语义，配置文件格式迁移放到任务八十七完成。
- `req_id_len`、协程和时间轮字段本阶段只作为配置字段暴露，不强制驱动对应子系统。

### 任务八十七：迁移分组式配置文件

已完成能力：

- `Config` 只支持当前项目自有分组式 XML。
- `conf/*.xml` 和 `generator/template/conf.xml.template` 迁移为 `server`、`network`、`log`、`coroutine`、`timewheel`、`rpc` 分组。
- 删除旧扁平字段读取路径，不再读取 `server_addr`、根级 `protocol`、根级 `timeout`、根级 `iothread_num` 和根级 `log_level`。
- `server.protocol` 只支持 `tinypb` / `http`。
- `log.max_size_mb` 和 `coroutine.stack_size_kb` 按单位转换为 bytes 保存。
- 生成器检查脚本改为验证 `<host>` 和 `<port>` 分组字段。

验证命令：
```bash
docker exec rpc-ubuntu bash -c "cd /workspace && rm -rf build && bash build.sh && ./build/test_config && ./build/test_start"
docker exec rpc-ubuntu bash -c "cd /workspace && ./scripts/check_generator.sh"
```

当前限制：

- 不兼容旧扁平 XML。
- 不兼容原 TinyRPC 的历史拼写字段，例如 `protocal`、`inteval`、`log_sync_inteval`、`msg_req_len`。
- 当前轻量 XML 解析辅助只覆盖本项目固定 schema，不引入 TinyXML。

### 任务八十八：实现双日志和日志事件

已完成能力：

- 新增 `LogType::RpcLog` 和 `LogType::AppLog`。
- 新增 `LogEvent`，保存日志类型、级别、时间、pid、tid、协程 id、文件、行号、函数、reqId 和 message。
- `Logger` 支持 RPC/APP 两个 sink，分别设置日志级别。
- 新增 APP 日志宏 `AppDebugLog`、`AppInfoLog`、`AppWarnLog`、`AppErrorLog`。
- 日志行格式固定为 `[time] [RPC|APP] [LEVEL] [pid=N] [tid=T] [co=N] [reqId=R] [file:line] [func=F] message`。
- 未显式传入 reqId 时，日志会自动读取当前 request context。

验证命令：
```bash
docker exec rpc-ubuntu bash -c "cd /workspace && rm -rf build && bash build.sh && ./build/test_log && ./build/test_runtime"
```

当前限制：

- 任务八十八只实现双通道和事件格式，周期 flush、shutdown drain 和 rolling 在任务八十九补齐。
- 没有请求上下文时仍输出空 reqId，保持日志格式稳定。

### 任务八十九：补齐异步日志生命周期

已完成能力：

- `Logger::init(logPath, prefix, rpcLevel, appLevel, async, syncIntervalMs, maxSizeBytes)` 支持完整初始化。
- 异步模式下业务线程完成过滤、格式化和入队，后台线程按日志类型写入 RPC/APP 文件。
- `flush()` 在异步模式下等待队列清空和当前写入完成后再 flush 两个文件。
- `shutdown()` drain 队列、flush、关闭文件、join worker、恢复控制台模式，并支持幂等调用和重新初始化。
- 按大小滚动 `${prefix}_rpc.log.N` 和 `${prefix}_app.log.N`。
- 多线程并发写入、shutdown drain、reinit 和小文件滚动都有测试覆盖。

验证命令：
```bash
docker exec rpc-ubuntu bash -c "cd /workspace && rm -rf build && bash build.sh && ./build/test_log"
docker exec rpc-ubuntu bash -c "cd /workspace && ./scripts/check_all.sh"
```

当前限制：

- 不按日期滚动。
- 不压缩归档。
- 不做跨进程日志锁。
- 不做远程日志采集。

### 任务九十：补全启动入口和运行时上下文

已完成能力：

- `start.h` 暴露 `GetConfig()`、`GetConstConfig()`、`GetServer()`、`GetIOThreadPoolSize()` 和 `AddTimerTask()`。
- `StartRpcServer()` 使用配置初始化 `Logger`，再按协议创建 TinyPB 或 HTTP server。
- `TcpServer` 支持 `addTimerTask()`，内部委托主 Reactor Timer。
- `Runtime::addTimerTask()` 和 `tinyrpc::AddTimerTask()` 委托当前 server，server 为空或 task 为空时返回 `false`。
- `RequestContext` 补齐 reqId、interface name、method name、local addr、peer addr 和 protocol type。
- TinyPB dispatcher 在成功解析 `serviceFullName` 后设置上下文，纯方法名写入 `methodName`。
- HTTP dispatcher 在调用 servlet 前设置上下文，reqId 来源为 `X-Req-Id` header。
- TinyPB/HTTP dispatcher 都使用 RAII guard 清理线程局部上下文。

验证命令：
```bash
docker exec rpc-ubuntu bash -c "cd /workspace && rm -rf build && bash build.sh && ./build/test_start && ./build/test_runtime"
docker exec rpc-ubuntu bash -c "cd /workspace && ./scripts/check_rpc_sync.sh && ./scripts/check_stage12_http.sh"
```

当前限制：

- 本阶段不实现 `TcpServer::stop()`；阶段 26 已补齐框架层停止入口。
- TinyPB/HTTP 的 local/peer 地址当前为 `"local"` / `"peer"` 占位，并已封装成后续可替换 helper。
- `AddTimerTask()` 投递到当前 server 主 Reactor Timer，不额外创建独立 timer runtime。

## 后续补全计划：简化实现完整化

当前任务一到任务九十已经完成，主链路通过 `scripts/check_all.sh` 收口。后续不再沿用原来的“从零复刻”路线，而是针对覆盖矩阵中标注为“简化复刻 / 暂不复刻”的模块进行第二轮补全。

后续计划入口：

- [TinyRPC 简化实现补全任务计划书](simplified-completion-task-plan.md)

阶段 21 已完成，下一次最适合开始的任务：

- **任务一百零六：HTTP request line、URL 和 query 解析补全**。

阶段 18 已完成配置、日志、启动入口和运行时上下文补全。阶段 19 已完成协程 hook、协程池和栈内存池补全。阶段 20 已完成 TcpClient Reactor 化、客户端 TcpConnection 语义、响应缓存和连接复用策略补全。阶段 21 已完成真实异步 RPC 网络路径，后续可以进入阶段 22 的 HTTP 协议栈补全。

## 阶段 20：TcpClient Reactor 化和客户端连接语义补全

已完成能力：

- `TcpConnection` 增加 `ServerConnection` / `ClientConnection` 类型区分，客户端连接持有 input buffer、output buffer、TinyPB codec、peer address 和按 reqId 索引的 response map。
- `TcpClient` 同步 TinyPB 请求复用客户端 `TcpConnection` 完成编码、缓冲区管理、响应解析和响应缓存读取。
- `TcpClient` 的 connect / write / read 等待路径改为当前线程 `Reactor`、`FdEvent` 和 `TimerTask` 驱动，不再把 `poll()` 作为同步超时核心。
- 客户端按当前 request reqId 等待匹配 response，错误 reqId 或未知 reqId 响应会记录并丢弃，不污染当前同步调用。
- `TcpClient` 默认复用已连接 fd，并提供 `setReuseConnection(false)` 与 `closeConnection()` 控制连接生命周期。
- 网络失败、超时、协议错误或对端关闭后会关闭当前连接，下一次请求自动重建，避免复用脏 buffer 或半关闭 fd。
- 新增 `scripts/check_rpc_client_reactor.sh`，集中回归同步客户端 Reactor 化、连接编解码、Timer/Reactor 依赖和同步 RPC 安全网。

验证命令：
```bash
docker exec rpc-ubuntu bash -c "cd /workspace && rm -rf build && bash build.sh && ./scripts/check_rpc_client_reactor.sh && ./scripts/check_rpc_sync.sh && ./scripts/check_all.sh"
wsl --cd "D:\codeproject\cpp\rpc" bash -lc "rm -rf build && bash build.sh && bash scripts/check_rpc_client_reactor.sh && MYTINYRPC_SKIP_BUILD=1 bash scripts/check_rpc_sync.sh && bash scripts/check_all.sh"
```

当前限制：

- 同步客户端仍保持单 in-flight 调用，不实现多个并发 pending request。
- 不实现连接池、负载均衡或异步 channel 的真实网络派发路径。
- 迟到或未知 reqId 响应默认丢弃，不提供跨调用业务级暂存策略。

## 阶段 19：协程 hook、协程池和栈内存池完整化

已完成能力：

- 新增线程局部 `SetHook()` / `IsHookEnabled()`，透明 hook 默认关闭，测试和排障可显式启停。
- 通过 `dlsym(RTLD_NEXT, ...)` 保存真实 `read/write/accept/connect/sleep/usleep`，透明入口关闭或主协程路径会直通真实系统调用。
- `FdEventContainer` 支持线程内 fd 到 `FdEvent` 的稳定索引，透明 IO hook 能自动绑定当前线程 Reactor。
- `CoroutinePool` 支持通过 `Config` 初始化初始容量、栈大小和耗尽扩展策略。
- `Coroutine` 支持外部栈，普通协程仍保持内部 `malloc/free` 栈兼容。
- `CoroutinePool` 内部协程栈接入 `FixedMemoryPool`，协程归还池时同步归还栈块。
- 新增 `scripts/check_coroutinehook.sh`，集中回归协程、hook、FdEvent、Reactor、协程池和栈内存池。

验证命令：
```bash
docker exec rpc-ubuntu bash -c "cd /workspace && rm -rf build && bash build.sh && ./scripts/check_coroutinehook.sh"
docker exec rpc-ubuntu bash -c "cd /workspace && ./scripts/check_all.sh"
```

当前限制：

- 透明 hook 只覆盖当前阶段验证过的 `read/write/accept/connect/sleep/usleep`。
- `recv/send` 保留显式 hook 入口，不提供 libc 同名透明入口。
- 不支持跨线程迁移协程，不实现 work stealing。
- 协程栈池不使用 `mmap` 和 guard page。

## 阶段 21：真正异步 RPC 网络路径

### 任务一百零一：异步 Channel 网络会话对象

已完成能力：

- 新增 `mytinyrpc/net/asyncclientsession.h` 和 `asyncclientsession.cc`，管理异步 Channel 的长生命周期客户端连接。
- `AsyncClientSession` 保存 peer addr、fd、客户端 `TcpConnection` 和 `TinyPbCodec`，负责 connect/sendRequest、EPOLLIN 读取回调和 disconnect。
- `TinyPbRpcAsyncChannel` 构造时持有 `AsyncClientSession`，替代每次 IOThread 任务中创建临时 `TcpClient`。
- 默认路径改为 session 在 IOThread 上执行 connect → sendRequest，响应由 EPOLLIN 读回调进入 pending map。
- `stop()` 先断开 session 再停止 IOThread，防止 fd 泄漏。
- `test_tinypb_rpc_async_channel` 和 `test_tinypb_async_client` 的测试服务器更新为 accept 一次后在同一连接上处理多个请求，验证 session 连接复用。
- `CMakeLists.txt` 新增 `asyncclientsession.cc` 编译目标。

验证命令：
```bash
./build.sh
./build/test_tinypb_rpc_async_channel
./build/test_tinypb_async_client
./scripts/check_rpc_async.sh
./scripts/check_rpc_sync.sh
```

当前限制：

- 任务一百零一只抽出 session 外壳；完整 EPOLLIN/EPOLLOUT 默认路径由后续任务完成。
- 不做连接池、负载均衡或重试策略。

### 任务一百零二：异步发送队列和 Reactor 写事件基础设施

已完成能力：

- `AsyncClientSession` 新增 `flushOutput()`、`registerFdEvent()`、`unregisterFdEvent()` 三个方法，作为 Reactor 异步写的基础设施。
- Session 新增 `m_reactor` 和 `m_fdEvent` 成员；`connect()` 获取当前线程 Reactor，`disconnect()` 先注销 FdEvent。
- `flushOutput()` 循环 send，遇到 EAGAIN 返回 false 供调用方注册 EPOLLOUT；写空后自动取消 EPOLLOUT。
- `registerFdEvent()` 注册 EPOLLOUT 到 Reactor，回调为 `flushOutput()`。
- `sendRequest()` 追加编码到 output buffer，`flushOutput()` 遇到 EAGAIN 后由 EPOLLOUT 继续推进。

验证命令：
```bash
./build.sh
./build/test_tinypb_rpc_async_channel
./build/test_tinypb_async_client
```

当前限制：

- 任务一百零二只建立写事件基础设施；完整默认异步路径由任务一百零三到一百零五收口。
- 不做背压策略或发送队列优先级。

### Task 103: Async read loop infrastructure and non-blocking socket

Capabilities added:

- AsyncClientSession socket switched to non-blocking after connect, enabling EPOLLIN/EPOLLOUT events.
- handleRead: reads from non-blocking socket, appends input buffer, decodes TinyPB frames, routes each response through readCallback.
- setReadCallback/startAsyncRead API: channel sets response handler before connect; EPOLLIN registered automatically.
- sendRequest appends request frames to output buffer and uses flushOutput / EPOLLOUT to drive writes.
- flushOutput removes only EPOLLOUT, preserving EPOLLIN state; unregisterFdEvent cleans both.
- Channel response delivery goes through EPOLLIN read callback and handleTinyPbResponse.

Verification:
```bash
./build.sh
./build/test_tinypb_rpc_async_channel
./build/test_tinypb_async_client
```

Current limitations:

- 不做流控和背压策略。

### Task 104: Timeout/cancel interrupting network state

Capabilities added:

- stop() calls failAllPending() after IOThread stop, failing all remaining pending with ERROR_RPC_CHANNEL_NETWORK.
- failAllPending drains pending map under lock, then cancels timeout tasks and runs done callbacks.
- IOThread task checks controller IsCanceled() before sendRequest; pre-canceled requests skip network I/O.
- All completion paths (response, timeout, cancel, stop) go through takePending one-time arbitration.
- 2 new tests: StopFailsAllPendingRequests (2 pending, stop, both get error), CancelBeforeSendSkipsNetwork (pre-cancel, done runs without network).

Verification:
./build/test_tinypb_rpc_async_channel  (12 tests)
./build/test_tinypb_async_client

Current limitations:

- timeout 当前使用独立等待线程驱动 pending 清理，尚未统一迁入 IOThread Reactor Timer。

### Task 105: Real TcpServer async RPC end-to-end verification

Capabilities added:

- test_tinypb_server_client --async-client mode: real TcpServer + TinyPbRpcAsyncChannel E2E test with success and cancel scenarios.
- check_rpc_async.sh orchestrates real server lifecycle (start, probe, async-client, kill).
- test_tinypb_rpc_async_channel adds pipelined-send coverage: the server must read multiple requests before first response.
- AsyncClientSession.shutdownSocket() remains as compatibility support for timeout/debug paths.
- Async Channel default path no longer waits in recvResponse; response completion is driven by EPOLLIN read callback.

Verification:
MYTINYRPC_SKIP_BUILD=1 ./scripts/check_rpc_async.sh
# [rpc-async] PASS (includes sync safety net)

Stage 21 complete: all 5 tasks done. Async Channel has session-based connection management, non-blocking socket infrastructure, EPOLLIN/EPOLLOUT default delivery, pending reqId matching, timeout/cancel/stop lifecycle, and real TcpServer E2E verification.

## 阶段 22：HTTP 协议栈补全

### 任务一百零六：HTTP request line、URL 和 query 解析补全

已完成能力：

- `HttpRequest` 新增 request target、query string 和 query map 字段。
- `HttpCodec` 支持解析 origin-form request target，例如 `/hello?name=alice`。
- `HttpCodec` 支持解析 absolute-form request target，例如 `http://example.com/api?q=rpc`。
- root path `/` 和空 query 可稳定解析。
- query 参数重复 key 采用后值覆盖先值的确定策略。
- request version 明确只接受 `HTTP/1.0` 和 `HTTP/1.1`。
- 非法 method、非法 version 或非法 request target 会解析失败并消费当前坏包头部，避免重复解析同一坏包。

验证命令：
```bash
./build.sh
./build/test_http_codec
```

当前限制：

- 不支持 CONNECT authority-form。
- 不支持 HTTP/2 pseudo header。
- query 参数当前不做 URL decode。

### 任务一百零七：HTTP header 和 body 解析增强

已完成能力：

- `HttpRequest` 的 header name 按小写规范化保存。
- `hasHeader()` 和 `getHeader()` 支持大小写不敏感查询。
- 同名 header 采用后值覆盖先值的确定策略。
- header value 在 codec 解析阶段去除首尾空白。
- `Content-Length` 支持大小写不敏感读取。
- `Content-Length` 非数字、负数、尾部脏字符或超过 `1 MiB` 时解析失败。
- POST 没有 body 时可按空 body 成功解析。
- body 半包仍保持不消费 buffer，等待后续字节补齐。

验证命令：
```bash
./build/test_http_codec
./scripts/check_stage12_http.sh
```

当前限制：

- 不支持 chunked body。
- 不支持 multipart。
- HTTP body 上限固定为 `1 MiB`，暂未接入配置项。

### 任务一百零八：HTTP response 默认 header 和错误响应

已完成能力：

- `HttpResponse` header name 按小写规范化保存，查询大小写不敏感。
- `HttpResponse::setErrorResponse()` 可统一生成 400、404、500 等错误响应。
- `HttpCodec::encode()` 在编码前统一修正 `Content-Length`。
- response 缺少 `Content-Type` 时默认补齐为 `text/plain; charset=utf-8`。
- response 编码时统一写入 `Connection: close`，覆盖调用方传入的 keep-alive。
- `HttpResponse::toString()` 只负责输出当前 response 字段，默认 header 由 encode 前的 `prepareForEncode()` 统一兜底。

验证命令：
```bash
./build/test_http_define
./build/test_http_codec
```

当前限制：

- 不支持 gzip。
- 不支持 streaming response。
- 当前阶段统一关闭 HTTP 连接，不实现 keep-alive。

### 任务一百零九：HTTP Servlet 分发补全

已完成能力：

- `HttpServlet::handle()` 改为返回 `bool`，成功返回 `true`，失败返回 `false`。
- `HttpDispatcher` 支持注册 `/` root servlet。
- 未注册 path 继续由默认 `NotFoundHttpServlet` 返回 404。
- 重复注册返回 `false`，并保留旧 servlet。
- servlet 返回 `false` 或抛出异常时，dispatcher 统一生成 500 响应。
- HTTP request context 中 `interfaceName` 固定为 `http`，`methodName` 保存 HTTP method 文本，新增 `path` 字段保存 request path。
- TinyPB request context 的 path 保持为空，原有接口调用点无需传入 path。

验证命令：
```bash
./build/test_http_dispatcher
./build/test_runtime
./build/test_start
```

当前限制：

- 不实现正则路由。
- 不实现 path parameter。
- servlet 异常只统一转成 500，不透出异常细节。

### 任务一百一十：HTTP 连接语义和脚本收口

已完成能力：

- `TcpConnection` 在服务端 HTTP 响应写完后主动关闭连接。
- TinyPB 服务端、TcpClient 客户端连接和无 codec Echo 路径保持原有连接语义。
- `test_http_server` 增加 `/hello?name=alice` query 验收路由。
- `test_http_server` 增加 `/submit` POST body 回显路由。
- `test_http_server` 增加 `/error` 500 错误响应路由。
- `scripts/check_stage12_http.sh` 覆盖 `/hello`、query、404、500、POST body、`Connection: close` 和 `Content-Length`。
- 新增 `docs/stage-22.md`，汇总 HTTP 当前支持矩阵和不支持范围。
- `docs/original-coverage-matrix.md` 将 HTTP 更新为已复刻核心语义。

验证命令：
```bash
./scripts/check_stage12_http.sh
./scripts/check_all.sh
```

当前限制：

- 不支持 HTTPS / HTTP2 / chunked / multipart。
- 不支持 streaming response。
- 不支持 keep-alive。

阶段 22 已完成。HTTP 协议栈已经具备 HTTP/1.0/1.1 GET/POST、origin-form/absolute-form request target、query map、大小写无关 header、Content-Length body、默认 response header、精确路径/root servlet、错误响应和统一关闭连接语义。下一步可以进入阶段 23，补齐生成器完整化。

## 阶段 23：生成器完整化

### 任务一百一十一：生成原项目风格工程目录

已完成能力：

- `tinyrpc_generator.py` 新增 `--layout` 参数，默认 `simple`，新增 `full`。
- `simple` layout 保留阶段 16 的平铺生成方式，兼容旧脚本和示例。
- `full` layout 生成 `bin/`、`conf/`、`log/`、`lib/`、`obj/`、`<project>/service/`、`<project>/interface/`、`<project>/pb/`、`<project>/comm/` 和 `test_client/`。
- `<project>` 默认由 service 名转为 snake_case，也可通过 `--project` 指定。
- 生成脚本的覆盖策略保持明确：重复生成时模板文件和 Protobuf 产物按当前模板覆盖写入。

验证命令：
```bash
./scripts/check_generator.sh
```

当前限制：

- `full` layout 仍依赖本地 MyTinyRPC 源码路径，不是发布级独立源码包。

### 任务一百一十二：生成器集成 protoc

已完成能力：

- 生成器检查当前 Linux 环境中的 `protoc`。
- 生成时复制输入 proto 到目标 pb 目录。
- 生成时调用 `protoc --cpp_out` 产出 `.pb.h` 和 `.pb.cc`。
- 同时生成 descriptor-set 文件。
- `protoc` 不存在、proto 文件不存在和 proto 语法错误都会输出明确 `[generator] FAIL: ...` 并返回非零。

验证命令：
```bash
./scripts/check_generator.sh
```

当前限制：

- 当前只生成 C++ Protobuf 代码，不支持多语言生成。

### 任务一百一十三：用 descriptor-set 解析 service/method

已完成能力：

- 生成器优先读取 descriptor-set，提取 package、service、method、request type 和 response type。
- 支持一个 proto 多个 service。
- `--service` 支持 `ServiceName` 和 `package.ServiceName` 两种写法。
- descriptor 解析失败时保留文本 parser fallback，用于简单一元 RPC 声明的保底解析。
- TinyPB dispatcher 的 `serviceFullName` 解析改为按最后一个 `.` 拆分，支持带 package 的 service 名。
- `scripts/check_generator.sh` 构造带 package 和双 service 的 proto，验证 descriptor service 选择、命名空间和多 method 生成。

验证命令：
```bash
./scripts/check_generator.sh
```

当前限制：

- 不支持 streaming RPC。
- 不支持 proto2 特殊语义。

### 任务一百一十四：生成 interface/service/business exception 模板

已完成能力：

- 新增 `business_exception.h.template`。
- 新增 `interface_base.h.template` 和 `interface_base.cc.template`。
- 每个 rpc method 生成独立 `<method>_interface.h/.cc`。
- service 实现类统一持有并调用对应 interface。
- service 层捕获 `BusinessException` 和标准异常，并写入 `RpcController`。
- 生成代码包含 request/response 参数用途注释。
- 生成代码支持 proto package 对应的 C++ namespace。

验证命令：
```bash
./scripts/check_generator.sh
```

当前限制：

- interface 默认实现只清空 response，不生成真实业务逻辑。

### 任务一百一十五：完整生成工程端到端验收

已完成能力：

- `CMakeLists.txt.template` 支持 simple/full 两种 layout。
- `full` layout 将 server/client 输出到 `bin/`，静态库输出到 `lib/`。
- `run.sh.template` 和 `shutdown.sh.template` 支持不同 layout 的二进制、配置和日志路径。
- 生成 `test_client/test_tinyrpc_client.cc`。
- `scripts/check_generator_project.sh` 同时验收 simple 和 full layout。
- 生成工程可构建、启动、调用和关闭。
- 新增 `docs/stage-23.md`，并更新 README、示例和覆盖矩阵。

验证命令：
```bash
./scripts/check_generator_project.sh
./scripts/check_generator.sh
./scripts/check_all.sh
```

当前限制：

- 不生成 IDE 工程。
- 不做交互式项目向导。
- 该阶段完成时，生成 server 的关闭仍由脚本 pid 管理，框架层暂未提供 `TcpServer::stop()`；阶段 26 已补齐生成 server 信号停止入口。

阶段 23 已完成。生成器已经具备 simple/full layout、`protoc` 产物生成、descriptor-set 元数据解析、package/service/method 识别、method interface、service 适配、test client 和生成工程端到端验收。下一步可以进入阶段 24，补齐可选插件、观测和性能边界。

## 阶段 24：可选插件、观测和性能边界

### 任务一百一十六：通用 ThreadPool 工具

已完成能力：

- 新增 `mytinyrpc/comm/thread_pool.*`，提供固定数量普通工作线程。
- `ThreadPool::start()` 启动工作线程，`addTask()` 接收 `std::function<void()>` 后由 worker 执行。
- `ThreadPool::stop()` 停止接收新任务，等待已入队任务执行完并 join 所有 worker。
- 析构函数自动调用 `stop()`，避免普通后台线程泄漏。
- stop 后再次 `addTask()` 返回 false，空任务和 0 线程池也有明确失败边界。
- 新增 `test_thread_pool`，覆盖并发执行、stop drain、析构 join 和无效线程数。

验证命令：
```bash
./build.sh
./build/test_thread_pool
```

当前限制：

- 不替换 `IOThreadPool`。
- 不实现优先级队列或动态扩缩容。

### 任务一百一十七：MySQL 插件配置和可选编译骨架

已完成能力：

- 新增 `MYTINYRPC_ENABLE_MYSQL` CMake option，默认关闭。
- 默认构建不会查找 MySQL/MariaDB client 开发库，普通回归不受外部数据库依赖影响。
- option 开启时查找 `mysql/mysql.h`、`mysql.h` 以及 `mysqlclient` / `mariadb` client 库。
- 缺少开发库时 CMake 输出明确错误，提示安装 `libmysqlclient-dev` 或 `libmariadb-dev`，也可关闭 option。
- `Config` 支持解析 `<mysql>` 配置段，缺失时使用关闭状态和默认 host、port、charset、timeout。
- 新增 `MySQLInstance` / `MySQLInstanceFactory`，插件关闭时提供 no-op 线程局部实例入口。
- 新增 `test_config` 覆盖 MySQL 默认配置和 XML 配置解析。
- 新增 `docs/stage-24.md` 记录可选插件边界。

验证命令：
```bash
./build.sh
./build/test_config
cmake -S . -B build-mysql-check -DMYTINYRPC_ENABLE_MYSQL=ON
```

当前限制：

- 不在默认回归中连接真实 MySQL server。
- 不实现 MySQL 连接池。

### 任务一百一十八：Tracing / request context 补全

已完成能力：

- `RequestContext` 新增 `traceId`、`path`、`protocolType` 和 `getProtocolName()`，`toString()` 输出完整上下文摘要。
- `Logger` 的 `LogEvent` 新增 traceId、interface、method、path、peer 和 protocol 字段。
- 日志写入时会自动读取当前线程 request context，未显式传入 reqId 时同时补齐 traceId 和请求信息。
- TinyPB 服务端 dispatcher 在业务方法执行期间设置 reqId、service、method、local、peer 和协议类型。
- HTTP dispatcher 在 servlet 执行期间设置 `interfaceName=http`、HTTP method 和 path。
- 同步 `TinyPbRpcChannel` 在客户端 Stub 调用期间设置 request context，并记录 peer 地址。
- 异步 `TinyPbRpcAsyncChannel` 在发送、响应、超时、取消和 stop 清理路径中设置 request context。
- 新增/扩展 `test_runtime`、`test_log`、`test_tinypb_rpc_channel` 和 `test_tinypb_rpc_async_channel` 覆盖上下文字段和清理边界。

验证命令：
```bash
./build.sh
./build/test_runtime
./build/test_log
./build/test_tinypb_rpc_channel
./build/test_tinypb_rpc_async_channel
./scripts/check_rpc_sync.sh
./scripts/check_rpc_async.sh
./scripts/check_stage12_http.sh
```

当前限制：

- 不实现 OpenTelemetry。
- 不做跨进程 trace 传播协议。

### 任务一百一十九：基础 benchmark 和资源生命周期检查

已完成能力：

- 新增 `benchmark_http`，覆盖 HTTP decode、dispatcher 和 encode 的简单吞吐统计。
- 新增 `benchmark_tinypb_sync`，通过真实 TinyPB frame 服务端统计同步 Stub 调用平均耗时。
- 新增 `benchmark_tinypb_async`，通过长连接 TinyPB frame 服务端统计异步 Channel 多请求完成吞吐。
- 新增 `scripts/check_resource_lifetime.sh`，集中运行 benchmark、日志生命周期、线程池生命周期和真实 TinyPB server fd 检查。
- 资源脚本会检查多次 client 调用后 server fd 数量不持续增长，并在退出后确认没有残留 server 进程。
- `.gitignore` 忽略 `build-*` 临时 CMake 目录，避免可选插件探测目录污染工作区。
- `CMakeLists.txt` 接入三个 benchmark 目标。

验证命令：
```bash
./build.sh
./scripts/check_resource_lifetime.sh
```

当前限制：

- benchmark 只输出参考统计，不作为严格性能门禁。
- 不引入外部压测工具或商业级压测报告。

阶段 24 已完成。可选插件、轻量观测和性能边界已经收口：普通构建不依赖 MySQL 或外部压测工具；tracing 以线程局部 request context 贯穿 TinyPB、HTTP、同步/异步客户端和日志；benchmark 与资源生命周期检查通过脚本提供低依赖验收入口。下一步可以进入阶段 25，更新覆盖矩阵、完整补全回归脚本和最终边界总结。

## 阶段 25：补全计划收口

### 任务一百二十：更新原 TinyRPC 覆盖矩阵

已完成能力：

- `docs/original-coverage-matrix.md` 更新为阶段 25 收口口径。
- 覆盖矩阵新增“补全任务编号”列，能直接追溯阶段 18 到阶段 24 的补全来源。
- 状态统一为“已复刻 / 已复刻核心语义 / 保留简化 / 可选未启用”。
- 每个模块都列出当前能力和验证入口。
- 保留边界表明确 MySQL 真实连接池、连接池/负载均衡、HTTP 高级能力、完整 tracing、商业级压测、高级 proto 语义和发布级打包不在本阶段继续补。

验证方式：

```bash
rg -n "补全任务编号|保留边界" docs/original-coverage-matrix.md
```

当前限制：

- 矩阵是学习复刻覆盖矩阵，不承诺与原 TinyRPC 100% 行为一致。
- 任务一百二十提交时完整补全脚本尚未创建，最终入口由任务一百二十一补齐。

### 任务一百二十一：新增完整补全回归脚本

已完成能力：

- 新增 `scripts/check_full_completion.sh`。
- 脚本串联 `check_all.sh`、`check_coroutinehook.sh`、`check_rpc_client_reactor.sh`、`check_rpc_async.sh`、`check_stage12_http.sh`、`check_generator_project.sh` 和 `check_resource_lifetime.sh`。
- `check_all.sh` 先完成构建，后续支持 skip build 的专项脚本使用 `MYTINYRPC_SKIP_BUILD=1` 减少重复构建。
- 脚本统一输出 `[full-completion] PASS`。
- `scripts/check_all.ps1` 新增 `-FullCompletion` 参数，Windows PowerShell 可通过 WSL 调用完整补全回归。
- README 增加完整补全回归入口。

验证命令：

```bash
./scripts/check_full_completion.sh
```

已验证结果：

- 2026-06-10 在 WSL 中通过，最终输出 `[full-completion] PASS`。

当前限制：

- 该脚本是本地 Linux/WSL 回归入口，不绑定云 CI。
- 生成工程和资源生命周期检查会让总耗时明显长于 `check_all.sh`。

### 任务一百二十二：README 和学习总结补全

已完成能力：

- README 新增 “Completed Supplement Capabilities” 章节，说明阶段 18 到阶段 25 补全能力。
- `docs/learning-summary.md` 新增阶段 18 到阶段 25 补全总结表。
- `examples/tinypb_sync/README.md` 说明阶段 20 同步客户端 Reactor 化完整路径。
- `examples/tinypb_async/README.md` 更新为阶段 21 真实 non-blocking EPOLLIN/EPOLLOUT 异步网络路径。
- `examples/http_server/README.md` 更新阶段 22 HTTP 补全能力和 `[stage12-http] PASS` 输出。
- `examples/generated_project/README.md` 说明阶段 23 simple/full layout、descriptor-set、interface/service 和生成工程端到端路径。
- 文档明确项目仍是学习实现，不升级为商业级 RPC 发布版。

验证方式：

```bash
rg -n "check_full_completion|Full-Completion Path|阶段 18 到阶段 25|Completed Supplement" README.md docs/learning-summary.md examples
```

当前限制：

- examples 仍复用测试程序和脚本作为可运行示例，不额外复制一套示例源码。
- README 和学习总结不是商业用户手册。

### 任务一百二十三：最终边界审计和收口提交

已完成能力：

- 执行 `rg -n "简化|暂不|TODO|placeholder|后续" docs mytinyrpc generator testcases` 审计剩余边界词。
- 将剩余项分为：
  - 已有计划且已执行：阶段 18 到阶段 25 的简化项补全记录。
  - 明确不做：MySQL 真实连接池、连接池/负载均衡、HTTP 高级能力、完整 tracing、商业级压测、高级 proto 语义、发布级独立工程打包。
  - 阶段 25 当时需要新计划：`TcpServer::stop()`（已由阶段 26 补齐）、HTTP keep-alive/chunked/streaming、服务发现/多目标 RPC、OpenTelemetry/跨进程 trace、发布级生成工程打包。
- `coroutinehook.cc` 清理历史 `TODO(task-92)` 注释，改为当前 FdEventContainer 挂起恢复说明。
- 生成器 `placeholder` 文案改为生成示例/测试客户端描述，避免被误判为未完成项。
- `docs/original-coverage-matrix.md` 新增最终边界审计表，回归入口更新为 `./scripts/check_full_completion.sh`。

验证命令：

```bash
rg -n "简化|暂不|TODO|placeholder|后续" docs mytinyrpc generator testcases
./scripts/check_full_completion.sh
git status --short
```

当前限制：

- 阶段 25 不继续补新代码能力，只做补全计划终点、脚本和边界收口。
- 剩余生产级能力需要新阶段或新计划承接。

阶段 25 已完成。第二轮“简化实现补全”现在有覆盖矩阵、完整补全回归脚本、README/学习总结/examples 入口和最终边界审计；验收以 `./scripts/check_full_completion.sh` 输出 `[full-completion] PASS` 为准。

## 阶段 26：TcpServer 优雅停止和生命周期收口

### 任务一百二十四：增加 `TcpServer` 优雅停止入口

已完成能力：

- `TcpServer` 新增 `stop()`，可从其他线程唤醒阻塞中的 `start()` 主 Reactor。
- `TcpServer` 新增 `isRunning()`，方便测试和上层代码观察事件循环状态。
- `TcpServer::start()` 退出后统一执行 shutdown，清理监听 fd、连接表和 IOThreadPool。
- 析构函数复用 `stop()` / shutdown，避免显式停止和析构维护两套关闭路径。
- `comm/start` 新增 `StopRpcServer()`，上层不必直接保存 `TcpServer` 指针即可触发停止。
- 生成 server 模板接入 `SIGTERM` / `SIGINT` handler，收到普通 kill 或 Ctrl-C 时调用 `StopRpcServer()`。

验证命令：

```bash
cmake --build build --target test_tinypb_server_client test_http_server
```

### 任务一百二十五：新增 TcpServer 生命周期验收

已完成能力：

- 新增 `test_tcpserver_lifecycle`，覆盖单 Reactor stop 唤醒、活跃连接清理、多 Reactor 连接清理、stop 幂等和端口释放。
- 新增 `scripts/check_stage26_lifecycle.sh`，可单独验证阶段 26 生命周期能力。
- `scripts/check_all.sh` 接入阶段 26 生命周期回归。

验证命令：

```bash
MYTINYRPC_SKIP_BUILD=1 ./scripts/check_stage26_lifecycle.sh
```

已验证结果：

- 2026-06-11 在 WSL 中通过，最终输出 `[stage26-lifecycle] PASS`。

### 任务一百二十六：修复连接协程生命周期保活

已完成能力：

- `TcpConnection` 的连接协程回调改为捕获 `weak_ptr`，避免连接对象通过自身协程回调形成引用环。
- `closeWithCallback()` 在连接协程内部触发时，把移除连接表动作延后到 Reactor task，避免当前协程返回前释放自身对象。
- `TcpConnection` 增加测试观察用存活计数，用于验证连接关闭后对象真正析构。
- `test_tcpserver_lifecycle` 新增对端关闭后连接对象不保活的回归用例。

验证命令：

```bash
./build/test_tcpserver_lifecycle
```

### 任务一百二十七：阶段 26 文档和覆盖矩阵收口

已完成能力：

- 新增 `docs/stage-26.md`，说明 `TcpServer::stop()`、`StopRpcServer()`、连接协程生命周期、生成工程信号停止和当前边界。
- README 新增阶段 26 生命周期验收入口和 stop 使用说明。
- `docs/original-coverage-matrix.md` 将 `TcpServer::stop()` 从“需要新计划”移入已完成 TCP/start/generator 能力。
- `docs/learning-summary.md`、`docs/project-structure.md` 和生成器 README 模板同步更新阶段 26 状态。

验证命令：

```bash
rg -n "TcpServer::stop|StopRpcServer|check_stage26_lifecycle|阶段 26" README.md docs generator/template
./scripts/check_stage26_lifecycle.sh
```

当前限制：

- `TcpServer::start()` 仍是阻塞式事件循环；异步启动由调用方放入独立线程。
- 同一 `TcpServer` 对象不提供 stop 后 restart 语义。
- HTTP keep-alive、服务发现、连接池、OpenTelemetry 和发布级安装包仍属于后续独立阶段。

## 阶段 27：RPC Channel API 对齐

阶段 27 的目标是让 RPC Channel 的对外使用体验更接近原 TinyRPC，同时继续使用当前项目已经补齐的同步 Reactor 客户端、异步 pending 仲裁和真实网络路径。

### 任务一百二十八：对齐同步 RPC Channel API

已完成能力：

- `TinyPbRpcChannel` 新增 `Ptr` 类型别名，业务代码可以使用 `TinyPbRpcChannel::Ptr` 持有 Channel。
- 新增 `std::shared_ptr<IPAddress>` 构造入口，对齐原 TinyRPC 中以共享地址对象创建 Channel 的用法。
- 同步 Channel 持有长生命周期 `TcpClient`，同一个 Channel 下连续 Stub 调用可以复用 TCP 连接。
- 新增 `setTimeout()` / `getTimeout()`，作为 Channel 级默认同步网络超时；单次调用仍可通过 controller timeout 覆盖。
- 新增 `setReuseConnection()` / `isReuseConnection()` / `closeConnection()`，明确同步 Channel 连接复用和关闭语义。
- `test_tinypb_rpc_channel` 新增 `PtrChannelReusesConnectionForSequentialStubCalls`，验证 `Ptr`、共享地址构造和同连接连续 RPC。

验证命令：

```bash
cmake --build build --target test_tinypb_rpc_channel -j2
./build/test_tinypb_rpc_channel
```

### 任务一百二十九：对齐异步 RPC Channel API

已完成能力：

- `TinyPbRpcAsyncChannel` 新增 `Ptr`、`ControllerPtr`、`MessagePtr` 和 `ClosurePtr` 类型别名。
- 新增 `std::shared_ptr<IPAddress>` 构造入口，与同步 Channel 的创建方式保持一致。
- 新增 `saveCallee()`，在下一次 Stub 调用前保存 controller、request、response 和 closure 的 shared_ptr 生命周期。
- 新增 `wait()` 和 `waitFor()`，等待 pending 清空并等待正在执行的完成回调退出。
- 新增 `getPeerAddress()`，方便生成代码和测试观察目标地址。
- `test_tinypb_rpc_async_channel` 新增 `SaveCalleeKeepsSharedObjectsAliveUntilWaitReturns`，验证共享对象保活、response 写回、closure 执行和 pending 清空。

验证命令：

```bash
cmake --build build --target test_tinypb_rpc_async_channel -j2
./build/test_tinypb_rpc_async_channel
MYTINYRPC_SKIP_BUILD=1 ./scripts/check_rpc_async.sh
```

### 任务一百三十：生成客户端和阶段文档收口

已完成能力：

- `generator/template/client.cc.template` 使用 `std::make_shared<IPAddress>`、`TinyPbRpcChannel::Ptr` 和 `setReuseConnection(true)`。
- `scripts/check_generator.sh` 对 simple/full 生成客户端增加 `TinyPbRpcChannel::Ptr` 和 `setReuseConnection(true)` 断言。
- 新增 `docs/stage-27.md`，记录同步/异步 Channel API、生成客户端改动和当前边界。
- README、学习总结、覆盖矩阵、项目结构和生成器示例说明同步记录阶段 27。

验证命令：

```bash
./scripts/check_generator.sh
./scripts/check_generator_project.sh
MYTINYRPC_SKIP_BUILD=1 ./scripts/check_rpc_sync.sh
MYTINYRPC_SKIP_BUILD=1 ./scripts/check_rpc_async.sh
```

当前限制：

- 同步 Channel 仍是单目标 RPC Channel，不做连接池、服务发现或负载均衡。
- 异步 Channel 仍使用一个 `AsyncClientSession` 和内部 `IOThread`，`saveCallee()` 只负责生命周期托管。
- 生成工程仍依赖 `MYTINYRPC_ROOT` 指向本地 MyTinyRPC 源码树；发布级独立工程打包由阶段 31 承接。
