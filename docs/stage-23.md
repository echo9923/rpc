# 阶段 23：生成器完整化

阶段 23 在阶段 16 的最小生成工程基础上，把生成器补齐为更接近原 TinyRPC 业务工程的工具。当前生成器保留 `simple` 平铺布局，同时新增 `full` 原项目风格布局，生成时主动调用 `protoc` 产出 C++ Protobuf 文件和 descriptor-set，并按 service/method 生成 service、interface、test client 和运行脚本。

## Layout 模式

生成器入口：

```bash
python3 generator/tinyrpc_generator.py \
  --proto testcases/test_tinypb_server.proto \
  --service QueryService \
  --out build/generated_manual_project
```

默认 `simple` layout 保留阶段 16 的平铺风格，适合最小示例和兼容旧脚本。新增 `full` layout：

```bash
python3 generator/tinyrpc_generator.py \
  --proto testcases/test_tinypb_server.proto \
  --service QueryService \
  --layout full \
  --out build/generated_manual_full_project
```

`full` layout 生成目录：

```text
bin/
conf/
log/
lib/
obj/
<project>/service/
<project>/interface/
<project>/pb/
<project>/comm/
test_client/
```

`<project>` 默认由 service 名转换为 snake_case，例如 `QueryService` 生成 `query_service`。可通过 `--project` 指定项目目录名。

## Protoc 流程

生成器会检查当前 Linux 环境中的 `protoc`：

- 将输入 `.proto` 复制到生成工程的 pb 目录。
- 调用 `protoc --cpp_out` 生成 `.pb.h` 和 `.pb.cc`。
- 同时生成 descriptor-set，供 service/method 元数据解析使用。
- `protoc` 不存在或 proto 语法错误时输出 `[generator] FAIL: ...` 并返回非零。

`scripts/check_generator.sh` 覆盖：

- 正常生成 `.pb.h`、`.pb.cc`、descriptor-set。
- 非法 proto 文件路径。
- 非法 proto 语法。
- 缺少 `protoc` 时的明确错误。

## Descriptor 解析

生成器优先读取 descriptor-set 提取：

- package。
- service。
- method。
- request type。
- response type。

支持一个 proto 中包含多个 service，可通过 `--service ServiceName` 或 `--service package.ServiceName` 选择目标 service。descriptor 解析失败时会退回文本 parser，文本 parser 仍只覆盖简单一元 RPC 声明，用于保底诊断。

TinyPB 分发器同步调整为按最后一个 `.` 拆分 `serviceFullName`，因此带 package 的调用名如 `stage23.BetaService.echo` 会被解析为 service `stage23.BetaService` 和 method `echo`。

## Full 业务工程模板

`full` layout 生成的业务层结构：

| 目录 | 内容 |
|---|---|
| `<project>/pb` | 输入 proto、`.pb.h`、`.pb.cc`、descriptor-set。 |
| `<project>/service` | `main.cc` 和 service 实现类，负责 Protobuf service 适配。 |
| `<project>/interface` | `InterfaceBase` 和每个 rpc method 对应的独立 interface 类。 |
| `<project>/comm` | `BusinessException`，供 interface 抛出业务错误。 |
| `test_client` | 生成的 TinyPB Stub 客户端。 |
| `conf` | 分组式 XML 配置。 |
| `bin` / `lib` / `log` / `obj` | 运行、库产物、日志和对象文件目录。 |

service 实现类负责：

1. 接收 Protobuf `request` 和 `response` 参数。
2. 调用对应 method interface。
3. 捕获 `BusinessException` 和标准异常并写入 `RpcController`。
4. 调用 `done->Run()`。

interface 默认实现只清空 response，业务方可以在对应 `*_interface.cc` 中读取 request 并填充 response。

## 端到端工程验收

`scripts/check_generator_project.sh` 会分别验证 simple 和 full layout：

```bash
./scripts/check_generator_project.sh
```

验收流程：

1. 调用生成器生成工程。
2. 使用 `MYTINYRPC_ROOT` 配置 CMake。
3. 构建生成 server 和 client。
4. 运行 `run.sh` 启动 server。
5. 运行生成 client 发起 TinyPB Stub 调用。
6. 运行 `shutdown.sh` 关闭 server。

脚本最终输出：

```text
[generator] PASS
```

## 验证命令

```bash
./scripts/check_generator.sh
./scripts/check_generator_project.sh
./scripts/check_all.sh
```

## 当前边界

- 当前只生成 C++ Protobuf 代码，不支持多语言生成。
- 当前只支持 unary RPC，不支持 streaming RPC。
- 当前不支持 proto2 特殊语义。
- 阶段 23 的 source 模式生成工程依赖本地 MyTinyRPC 源码，通过 `MYTINYRPC_ROOT` 指定；阶段 31 已补齐 `--package release` 发布级源码包模式。
- 业务 interface 默认返回空 proto3 response，不生成真实业务字段填充逻辑。
- 不生成 IDE 工程或交互式项目向导。
