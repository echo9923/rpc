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

## 当前边界

- 当前只解析阶段 13 启动所需的简单标签，不实现完整 XML schema 校验。
- 当前不读取 MySQL 插件、线程命名、日志文件滚动等复杂配置。
- 当前不做旧配置兼容或历史配置迁移。

## 验证命令

```bash
./build.sh
./build/test_config
./scripts/check_rpc_sync.sh
```
