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

## 当前边界

- 当前任务不读取 XML。
- 当前不做复杂校验。
- 当前只提供只读访问接口，后续 XML 配置读取任务会补充加载逻辑。

## 验证命令

```bash
./build.sh
./build/test_config
./scripts/check_rpc_sync.sh
```
