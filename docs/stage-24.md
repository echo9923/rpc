# 阶段 24：可选插件、观测和性能边界

阶段 24 补齐原 TinyRPC 项目中偏生产外壳的能力：通用线程池、可选 MySQL 插件、轻量 tracing/request context，以及基础 benchmark 和资源生命周期检查。

本阶段的核心约束是“可选”：普通学习、构建和回归不能因为机器缺少 MySQL、压测工具或外部服务而失败。

## 通用 ThreadPool

`ThreadPool` 位于 `mytinyrpc/comm/thread_pool.*`，用于普通后台任务，不替换 `IOThreadPool`。

| 工具 | 职责 |
|---|---|
| `ThreadPool` | 固定数量普通工作线程，执行 `std::function<void()>` 任务。 |
| `IOThreadPool` | 持有 `IOThread` 和 Reactor，处理网络 fd 事件。 |

当前能力：

- `start()` 启动固定数量工作线程。
- `addTask()` 添加普通后台任务。
- `stop()` 停止接收新任务，等待已入队任务执行完并 join 工作线程。
- 析构函数自动调用 `stop()`，避免线程泄漏。
- stop 后再次 `addTask()` 返回 false。

验证命令：

```bash
./build.sh
./build/test_thread_pool
```

## MySQL 可选插件

MySQL 插件默认关闭，普通构建不会查找 MySQL/MariaDB client 开发库。

默认构建：

```bash
./build.sh
./build/test_config
```

开启插件：

```bash
cmake -S . -B build -DMYTINYRPC_ENABLE_MYSQL=ON
```

开启后 CMake 会查找 `mysql/mysql.h`、`mysql.h` 以及 `mysqlclient` / `mariadb` client 库。缺少开发库时，CMake 会明确提示安装 `libmysqlclient-dev` 或 `libmariadb-dev`，也可以改回 `-DMYTINYRPC_ENABLE_MYSQL=OFF`。

### 配置段

XML 可选添加：

```xml
<mysql>
    <enable>true</enable>
    <host>127.0.0.1</host>
    <port>3306</port>
    <user>rpc_user</user>
    <password>rpc_pass</password>
    <database>rpc_db</database>
    <charset>utf8mb4</charset>
    <connect_timeout_ms>5000</connect_timeout_ms>
</mysql>
```

缺失 `<mysql>` 段时默认值为：

| 字段 | 默认值 |
|---|---|
| `enable` | `false` |
| `host` | `127.0.0.1` |
| `port` | `3306` |
| `charset` | `utf8mb4` |
| `connect_timeout_ms` | `5000` |

### No-op 边界

`MySQLInstanceFactory::getThreadLocalInstance()` 提供线程局部实例入口。

当插件关闭时：

- `MySQLInstanceFactory::isPluginEnabled()` 返回 false。
- `MySQLInstance::connect()` 返回 false。
- `getLastError()` 明确提示使用 `-DMYTINYRPC_ENABLE_MYSQL=ON` 重新构建。

当前不包含真实连接池，也不在默认回归中连接真实 MySQL server。

## 轻量 tracing / request context

阶段 24 把 request context 从早期的 reqId 扩展为轻量追踪上下文。它仍然是进程内、线程局部的上下文，不实现 OpenTelemetry 或跨进程 trace 传播协议。

当前字段：

| 字段 | 含义 |
|---|---|
| `traceId` | 追踪号；未单独传入时复用 reqId。 |
| `reqId` | TinyPB 请求号或 HTTP 请求上下文编号。 |
| `interfaceName` | TinyPB service 名或 `http`。 |
| `methodName` | TinyPB method 名或 HTTP method。 |
| `path` | HTTP path；TinyPB 路径为空。 |
| `localAddr` | 当前侧地址描述。 |
| `peerAddr` | 对端地址描述。 |
| `protocolType` | TinyPB 或 HTTP。 |

接入路径：

- TinyPB 服务端 dispatcher 在业务方法执行期间设置 context。
- HTTP dispatcher 在 servlet 执行期间设置 context。
- 同步 `TinyPbRpcChannel` 在客户端调用期间设置 context，并记录 peer 地址。
- 异步 `TinyPbRpcAsyncChannel` 在发送、响应、超时、取消和 stop 清理路径中设置 context。
- `Logger` 写日志时会自动读取当前 context，补齐 traceId、reqId、interface、method、path、peer 和 protocol。

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

## 基础 benchmark 和资源生命周期检查

阶段 24 新增三个基础 benchmark 目标。它们只用于观察明显退化，不作为严格性能门禁，也不依赖外部压测工具。

| 目标 | 关注点 |
|---|---|
| `benchmark_http` | HTTP decode、dispatcher 和 encode 的简单吞吐。 |
| `benchmark_tinypb_sync` | 同步 TinyPB Stub 调用的平均耗时。 |
| `benchmark_tinypb_async` | 异步 TinyPB Channel 多请求完成吞吐。 |

`scripts/check_resource_lifetime.sh` 会执行以下检查：

- benchmark 程序可以运行并输出参考统计。
- `test_log` 能完成异步日志 flush、shutdown 和重新初始化。
- `test_thread_pool` 能完成线程池析构 join 和 stop drain。
- 真实 TinyPB server 在多次 client 调用后 fd 数量不持续增长。
- 脚本停止 server 后不会留下残留 server 进程。

验证命令：

```bash
./build.sh
./scripts/check_resource_lifetime.sh
```

脚本通过时输出：

```text
[resource] PASS
```

## 阶段完成状态

阶段 24 已完成：

- 通用 `ThreadPool` 已接入。
- MySQL 插件具备可选编译开关、配置解析和 no-op 边界。
- 轻量 request context 已贯穿 TinyPB、HTTP、同步客户端、异步客户端和日志。
- 基础 benchmark 和资源生命周期检查脚本已接入。

当前仍保留的边界：

- MySQL 默认关闭，不在默认回归中连接真实 MySQL server。
- tracing 是轻量进程内上下文，不是完整分布式追踪系统。
- benchmark 仅输出参考统计，不做商业级压测报告。
