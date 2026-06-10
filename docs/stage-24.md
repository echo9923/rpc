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

## 当前阶段剩余项

- 补齐 tracing / request context，让 TinyPB、HTTP、同步客户端、异步客户端和日志都能看到一致请求上下文。
- 增加基础 benchmark 与资源生命周期检查脚本。
