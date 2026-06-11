# 阶段 31：发布级生成工程打包

阶段 31 的目标是让生成工程具备可交付的源码包形态：生成目录可以自带 MyTinyRPC 框架源码子集，在没有 `-DMYTINYRPC_ROOT=/path/to/rpc` 的情况下完成 CMake 配置、构建、启动、调用和关闭。这个阶段依赖阶段 26 的生成 server 停止入口，也会继续检验阶段 27 对齐后的 RPC Channel API。

## 任务一百三十一：生成器支持发布级源码包模式

已完成能力：

- `generator/tinyrpc_generator.py` 新增 `--package source|release` 参数，默认仍是 `source`，保持旧脚本和旧手工命令兼容。
- `--package release` 会在输出目录生成 `third_party/mytinyrpc/`，复制当前生成工程构建所需的 `comm`、`coroutine`、`net` 源码和头文件。
- 发布包会生成 `third_party/MYTINYRPC_MANIFEST.md`，列出随包复制的 MyTinyRPC 源码文件。
- 生成工程 CMake 优先探测 `third_party/mytinyrpc`，存在时自动作为 `MYTINYRPC_ROOT` 使用；不存在时仍支持用户显式传入 `-DMYTINYRPC_ROOT=...`。
- 生成工程 CMake 源码列表补齐 `comm/thread_pool.cc`、`comm/mysql_instance.cc` 和 `net/asyncclientsession.cc`，与当前主工程核心库保持一致。
- `generator/template/README.md.template` 根据 package 模式生成不同构建说明，release 模式不再要求用户传 `MYTINYRPC_ROOT`。

验证命令：

```bash
./scripts/check_generator.sh
```

## 任务一百三十二：新增发布包生成工程验收

已完成能力：

- 新增 `scripts/check_generator_release_package.sh`，专门验证 release package 生成工程。
- 脚本生成 full layout release 工程，断言 `third_party/mytinyrpc`、manifest、`TinyPbRpcChannel::Ptr` 和 `setReuseConnection(true)` 均存在。
- 脚本执行 `cmake -S <out> -B <out>/build`，不传 `MYTINYRPC_ROOT`。
- 脚本构建生成 server/client，启动 server，运行生成 client 发起 TinyPB Stub 调用，再通过 `shutdown.sh` 关闭 server。
- `scripts/check_all.sh` 和 `scripts/check_full_completion.sh` 接入发布包回归。

验证命令：

```bash
./scripts/check_generator_release_package.sh
```

已验证结果：

- 2026-06-11 在 WSL 中通过，最终输出 `[generator-release] PASS`。

## 任务一百三十三：阶段三十一文档和覆盖矩阵收口

已完成能力：

- README、学习总结、覆盖矩阵、项目结构和生成工程示例记录 `--package release` 与 `check_generator_release_package.sh`。
- `docs/original-coverage-matrix.md` 将发布级生成工程打包从保留边界中移出，纳入 generator 已完成能力。
- `docs/replica-progress.md` 记录阶段 31 的任务拆分、验证入口和当前边界。

验证命令：

```bash
rg -n "stage-31|阶段 31|check_generator_release_package|--package release|MYTINYRPC_MANIFEST" README.md docs examples generator scripts
./scripts/check_generator_release_package.sh
./scripts/check_generator.sh
```

## 当前边界

- release package 是源码包模式，不生成二进制安装器、系统服务单元、deb/rpm 或 IDE 工程。
- 发布包随带的是当前生成工程需要的 MyTinyRPC 源码子集，不复制仓库测试、文档、e2e、benchmark 或构建产物。
- 生成器仍只面向 C++ unary RPC，不支持 streaming RPC、proto2 特殊语义或多语言生成。
- `source` 模式仍保留，适合本仓库内学习和调试；`release` 模式适合把生成工程目录独立拿出构建。
