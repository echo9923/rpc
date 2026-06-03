# 阶段 18：配置、日志、启动入口和运行时补全

阶段 18 把 `comm/config`、`comm/log`、`comm/start` 和 `comm/runtime` 从早期最小外壳补齐为当前项目可复用的框架启动层。配置统一迁移为当前项目自有的分组式 XML，日志区分 RPC/APP 两条通道，启动入口暴露配置、server、IOThread 和 TimerTask 门面，TinyPB/HTTP dispatcher 在请求生命周期内设置线程局部 request context。

## 分组式 XML

当前只支持分组式 XML，不兼容旧扁平配置，也不兼容原 TinyRPC 历史拼写字段。

```xml
<config>
    <server>
        <host>127.0.0.1</host>
        <port>24139</port>
        <protocol>tinypb</protocol>
    </server>
    <network>
        <iothread_num>2</iothread_num>
        <timeout_ms>3000</timeout_ms>
        <max_connect_timeout_ms>5000</max_connect_timeout_ms>
    </network>
    <log>
        <path>logs</path>
        <prefix>mytinyrpc</prefix>
        <max_size_mb>64</max_size_mb>
        <rpc_level>info</rpc_level>
        <app_level>debug</app_level>
        <sync_interval_ms>1000</sync_interval_ms>
    </log>
    <coroutine>
        <stack_size_kb>128</stack_size_kb>
        <pool_size>128</pool_size>
    </coroutine>
    <timewheel>
        <bucket_num>60</bucket_num>
        <interval_sec>1</interval_sec>
    </timewheel>
    <rpc>
        <req_id_len>20</req_id_len>
    </rpc>
</config>
```

解析规则：

- `server.host` 缺失时默认 `127.0.0.1`，`server.port` 缺失时默认 `19999`。
- `server.protocol` 只接受 `tinypb` 或 `http`。
- `log.rpc_level` 和 `log.app_level` 只接受 `debug`、`info`、`warn`、`error`，大小写不敏感。
- 数字字段使用严格解析，非数字、尾部脏字符和越界都会使 `loadFromXml()` 返回 `false`，错误信息包含分组和字段。
- `log.max_size_mb` 保存为 bytes，`coroutine.stack_size_kb` 保存为 bytes。

## 默认值

| 字段 | Getter | 默认值 |
|---|---|---|
| server host | `getServerHost()` | `127.0.0.1` |
| server port | `getServerPort()` | `19999` |
| protocol | `getProtocol()` | `tinypb` |
| IOThread 数量 | `getIOThreadNum()` | `0` |
| RPC timeout | `getTimeoutMs()` | `5000` |
| 日志目录 | `getLogPath()` | `logs` |
| 日志前缀 | `getLogPrefix()` | `mytinyrpc` |
| 日志滚动大小 | `getLogMaxSizeBytes()` | `64 * 1024 * 1024` |
| RPC 日志级别 | `getRpcLogLevel()` / `getLogLevel()` | `LogLevel::Debug` |
| APP 日志级别 | `getAppLogLevel()` | `LogLevel::Debug` |
| 日志 flush 间隔 | `getLogSyncIntervalMs()` | `1000` |
| 协程栈大小 | `getCoroutineStackSizeBytes()` | `128 * 1024` |
| 协程池大小 | `getCoroutinePoolSize()` | `128` |
| 请求号长度 | `getReqIdLen()` | `20` |
| 最大连接超时 | `getMaxConnectTimeoutMs()` | `5000` |
| 时间轮桶数 | `getTimeWheelBucketNum()` | `60` |
| 时间轮间隔 | `getTimeWheelIntervalSec()` | `1` |

## RPC/APP 双日志

`Logger` 现在维护两类 sink：

- RPC 日志：`${logPath}/${prefix}_rpc.log`
- APP 日志：`${logPath}/${prefix}_app.log`

兼容入口 `Logger::init(path, level, async)` 仍保留，用于单 RPC 文件；新入口支持分别设置 RPC/APP 级别、异步开关、flush 周期和滚动大小。

```cpp
Logger::init(logPath, prefix, rpcLevel, appLevel, async);
Logger::init(logPath, prefix, rpcLevel, appLevel, async, syncIntervalMs, maxSizeBytes);
Logger::setLevel(LogType::RpcLog, LogLevel::Warn);
Logger::setLevel(LogType::AppLog, LogLevel::Debug);
```

RPC 宏：

- `DebugLog`
- `InfoLog`
- `WarnLog`
- `ErrorLog`

APP 宏：

- `AppDebugLog`
- `AppInfoLog`
- `AppWarnLog`
- `AppErrorLog`

日志事件由 `LogEvent` 保存，格式化后写入文件：

```text
[YYYY-MM-DD HH:MM:SS] [RPC|APP] [DEBUG|INFO|WARN|ERROR] [pid=N] [tid=T] [co=N] [reqId=R] [file:line] [func=F] message
```

`reqId` 未显式传入时，`Logger` 会读取 `getRuntime().getCurrentRequestContext().getReqId()`；没有请求上下文时仍输出 `[reqId=]`，便于测试和排查。

## 异步日志生命周期

同步模式下，业务线程通过 level 判断后直接写入对应文件缓冲，`Logger::flush()` 刷新文件。

异步模式下，业务线程完成 level 判断和格式化后把 `{LogType, line}` 入队，后台线程负责写入 RPC 或 APP 文件。后台线程唤醒条件包括：

- 队列非空。
- `shutdown()` 进入停止流程。
- 到达 `syncIntervalMs` 周期，需要周期性 flush。

`Logger::flush()` 在异步模式下会等待队列清空且当前写入完成，再 flush 两个文件。

`Logger::shutdown()` 是幂等的，语义为停止后台循环、drain 队列、flush、关闭文件、join worker，并恢复控制台模式。shutdown 后未重新 init 的日志不会继续写旧文件；重新 init 后可以使用新的 prefix 或路径。

滚动策略：

- `maxSizeBytes <= 0` 表示不滚动。
- 写入每一行前检查 `current_size + line_size` 是否超过阈值。
- 超过阈值时关闭当前文件，把 `${prefix}_rpc.log` 改名为 `${prefix}_rpc.log.1`；若 `.1` 已存在，使用 `.2`、`.3` 递增后缀。
- APP 文件同样按 `${prefix}_app.log.N` 滚动。
- 当前不按日期滚动、不压缩、不做跨进程文件锁。

## Start/Runtime 门面

业务入口可以通过 `start.h` 使用当前框架门面：

```cpp
bool InitConfig(const std::string& path);
bool StartRpcServer();
Config& GetConfig();
const Config& GetConstConfig();
TcpServer::Ptr GetServer();
int GetIOThreadPoolSize();
bool AddTimerTask(const std::shared_ptr<TimerTask>& task);
TinyPbDispatcher::Ptr GetTinyPbDispatcher();
HttpDispatcher::Ptr GetHttpDispatcher();
```

`StartRpcServer()` 会从配置读取日志目录、prefix、RPC/APP 级别、flush 间隔和滚动大小来初始化 `Logger`，再按 `server.protocol` 创建 TinyPB 或 HTTP server。`GetIOThreadPoolSize()` 返回 `GetConfig().getIOThreadNum()`。`AddTimerTask()` 委托当前 server 的主 Reactor Timer；server 为空或 task 为空时返回 `false`。

## RequestContext 生命周期

`RequestContext` 是线程局部请求上下文，当前字段为：

```cpp
const std::string& getReqId() const;
const std::string& getInterfaceName() const;
const std::string& getMethodName() const;
const std::string& getLocalAddr() const;
const std::string& getPeerAddr() const;
ProtocolType getProtocolType() const;
```

TinyPB dispatcher 生命周期：

```text
TinyPbStruct
    -> parse serviceFullName
    -> RequestContextGuard(reqId, serviceName, methodName, local, peer, TinyPb)
    -> find service / method
    -> ParseFromString
    -> Service::CallMethod
    -> SerializeToString
    -> sendProtocolData
    -> guard 析构并 clear context
```

HTTP dispatcher 生命周期：

```text
HttpRequest
    -> RequestContextGuard(X-Req-Id, "http", path, local, peer, Http)
    -> find servlet
    -> servlet->handle()
    -> encode response
    -> guard 析构并 clear context
```

两个 dispatcher 都使用 RAII guard 确保正常返回、业务失败和提前返回时清理上下文。`Logger` 通过该上下文自动补齐 reqId。

## 验证命令

阶段 18 收口时使用 Docker 环境执行：

```bash
docker exec rpc-ubuntu bash -c "cd /workspace && rm -rf build && bash build.sh"
docker exec rpc-ubuntu bash -c "cd /workspace && ./build/test_config && ./build/test_log && ./build/test_start && ./build/test_runtime"
docker exec rpc-ubuntu bash -c "cd /workspace && ./scripts/check_generator.sh && ./scripts/check_generator_project.sh"
docker exec rpc-ubuntu bash -c "cd /workspace && ./scripts/check_rpc_sync.sh && ./scripts/check_stage12_http.sh"
docker exec rpc-ubuntu bash -c "cd /workspace && ./scripts/check_all.sh"
```

## 当前限制

- 不兼容旧扁平 XML。
- 不兼容原 TinyRPC 的 `protocal`、`inteval`、`log_sync_inteval`、`msg_req_len` 等历史字段名。
- 不实现配置热加载、MySQL 插件、跨进程日志锁、压缩归档或远程日志采集。
- 不实现 `TcpServer::stop()`。
- TinyPB/HTTP request context 的 local/peer 当前保留为 `"local"` / `"peer"` 占位，并通过 helper 封装，后续可替换为真实连接地址。
- `req_id_len`、协程池、时间轮配置已暴露到 `Config`，但本阶段不强制改造 `ReqIdUtil`、协程池初始化或 `TcpConnectionTimeWheel`。
