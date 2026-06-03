# 阶段 13：配置、日志、启动入口和运行时

阶段 13 的目标是把测试程序逐步升级为更接近框架使用方式的启动模型：配置对象提供默认值并读取 XML，日志支持文件输出和级别，启动入口可以选择 TinyPB 或 HTTP server，运行时保存最小请求上下文。

## 任务六十三：最小 Config 默认值整理

已完成能力：

- 新增 `Config`，保存框架启动所需的最小配置。
- 默认 server host 为 `127.0.0.1`。
- 默认 server port 为 `19999`。
- 默认 protocol 为 `tinypb`。
- 默认 IOThread 数量为 `0`，表示沿用单 Reactor 模式。
- 默认 timeout 为 `5000` ms。
- 默认 log level 为 `LogLevel::Debug`。
- 新增 `test_config`，覆盖全部默认值，并验证默认配置可用于初始化测试 server。

## 任务六十四：XML 配置读取

已完成能力：

- `Config::loadFromXml()` 支持读取 XML 配置文件。
- 支持 `<server_addr>`，格式为 `host:port`；只写 `host` 时端口继续使用默认值。
- 支持 `<protocol>`，当前可选值为 `tinypb` 和 `http`。
- 支持 `<iothread_num>`，缺失时默认使用 `0`。
- 支持 `<timeout>`，缺失时默认使用 `5000` ms。
- 支持 `<log_level>` 和 `<log>`，当前可选值为 `debug`、`info`、`warn`、`error`。
- 缺失字段保留当前 `Config` 对象中的默认值。
- 非法路径或非法字段值会返回 `false`，并通过 `getLastError()` 暴露错误文本。
- 新增 TinyPB 与 HTTP 两份 XML 样例，`test_config` 验证两类 server 都可以从 XML 配置初始化。

## 任务六十五：日志系统分步实现

已完成能力：

- `Logger::init()` 支持初始化同步文件日志。
- 支持 `DEBUG`、`INFO`、`WARN`、`ERROR` 四级日志，并按最小级别过滤。
- 日志格式包含时间、级别、线程 id、文件行号和正文。
- `Logger::log()` 支持附加 `reqId`，便于 RPC 调试时关联请求号、方法名和错误码。
- `Logger::flush()` 可强制刷新文件缓冲，测试和调试时可以立即读取日志文件。
- `Logger::setEnabled(false)` 可临时关闭日志输出。
- 支持简化异步模式，业务线程写入队列，后台线程最终写入文件；`flush()` 和 `shutdown()` 会等待队列落盘。
- 未显式初始化文件日志时，继续输出到控制台，保持现有调试输出可见。
- 新增 `test_log`，覆盖级别过滤、文件输出、flush、关闭日志和异步落盘。

## 任务六十六：启动入口和服务注册宏

已完成能力：

- 新增 `Runtime`，保存启动期配置、codec、dispatcher 和 `TcpServer`。
- 新增 `InitConfig(path)`，读取 XML 配置。
- 新增 `StartRpcServer()`，根据配置中的 `protocol` 创建 TinyPB 或 HTTP server，并完成 `TcpServer::init()`。
- 新增 `GetServer()`，调用方可在注册完成后执行 `GetServer()->start()` 进入阻塞事件循环。
- 新增 `REGISTER_SERVICE(ServiceType)`，把 Protobuf Service 注册到 TinyPB server。
- 新增 `REGISTER_HTTP_SERVLET(path, ServletType)`，把 HTTP servlet 注册到 HTTP dispatcher。
- 新增 `test_start`，覆盖 XML 启动 TinyPB/HTTP server、服务注册宏和 HTTP servlet 注册宏。

## 任务六十七：运行时 request context

已完成能力：

- `Runtime` 新增线程局部 `RequestContext`，保存当前请求号、方法名、local addr 和 peer addr。
- `TinyPbDispatcher` 在调用业务 Service 前设置上下文，并通过 RAII 在请求结束时清理。
- 多线程请求上下文互不污染，每个线程读取自己的 request context。
- `Logger` 在未显式传入 `reqId` 时，会自动读取当前线程 request context 中的请求号。
- 新增 `test_runtime`，覆盖业务处理期间读取 reqId、请求结束清理、多线程隔离和日志自动打印 reqId。

## 当前边界

- 当前只解析阶段 13 启动所需的简单标签，不实现完整 XML schema 校验。
- 当前不读取 MySQL 插件、线程命名等复杂配置。
- 当前不做按大小滚动、压缩归档或多日志文件拆分。
- 当前 `StartRpcServer()` 只负责创建并初始化 server，不进入阻塞事件循环；真正运行由 `GetServer()->start()` 显式触发。
- 当前不做复杂生命周期管理器或插件系统。
- 当前 request context 只做进程内线程局部上下文，不做完整 tracing 或分布式链路追踪。
- 当前 local/peer addr 使用简化字段，后续连接层暴露真实地址后再替换来源。
- 当前不做旧配置兼容或历史配置迁移。

## 验证命令

```bash
./build.sh
./build/test_config
./build/test_log
./build/test_start
./build/test_runtime
./scripts/check_rpc_sync.sh
```
