# Repository Guidelines

## 适用范围

本文件适用于整个仓库。编码、修改文档或调整工程配置前，请先查看 [编码规范](docs/40-工程规范/编码规范.md)。

## 代码实现原则

### 编码规范必须严格遵守

1. 编码规范（参见 [编码规范](docs/40-工程规范/编码规范.md)）是代码编写的最高准则，**必须严格遵守**，不得以任何理由偏离。
2. 所有代码实现前，必须先完整阅读并理解编码规范。
3. 除非用户明确要求，修改代码时不需要兼容旧接口、旧配置、旧行为、旧流程或历史实现；优先采用直接、清晰、易维护的方案。
4. 项目中的现有数据默认视为无业务价值的测试数据；通常不需要考虑历史数据保留、迁移、回填、平滑升级或兼容处理，必要时可以直接重建。
5. 代码必须写注释，尤其是使用了系统调用的函数，要讲清楚系统调用的参数的含义以及作用。

## 单元测试规范

1. 新增单元测试文件应优先使用 GoogleTest 框架编写。
2. 测试文件放在 `testcases/` 目录，文件名保持英文小写，新增测试文件优先使用 `test_*.cc` 命名。
3. 测试用例使用 `TEST(TestSuiteName, TestCaseName)` 组织。
4. 测试断言优先使用 `EXPECT_*` 和 `ASSERT_*` 宏。
5. 现有测试文件暂不迁移；只有新增或重构测试时，再按 GoogleTest 规范处理。


## 提交规范

1. `git commit` 的提交记录必须使用中文。
2. 提交标题与正文统一使用 `UTF-8` 编码，默认按 `UTF-8，无 BOM` 处理。

## Markdown Mermaid 规范

1. Mermaid 节点或时序图消息里需要换行时，统一使用 `<br/>`。
2. 不要写字面量 `\n`，避免在当前 Markdown 预览里无法正常换行。

## 文档索引

### 总入口

- [阶段 1：阻塞式 TCP Echo 服务器](docs/stage-1.md)
- [阶段 5：连接层协议接入](docs/stage-5.md)
- [编码规范](docs/40-工程规范/编码规范.md)


## 构建要求

本项目以 Linux 作为目标运行环境。编译、运行、测试和阶段验收必须在 Linux 环境内执行。

### 环境选择

1. Windows 环境下使用 WSL。
2. macOS 等非 WSL 环境下使用 Docker Linux 容器。
3. Apple Silicon Mac 使用 Docker 时，容器应使用 `linux/amd64` 平台，以匹配项目当前的 x86-64 汇编实现。

### 预配置 Docker 容器（推荐）

项目已有一个名为 `rpc-ubuntu` 的 Docker 容器（基于 `ubuntu:24.04`），已安装 `build-essential`、`cmake`、`libgtest-dev` 等全部构建依赖，**编译、测试和验收应直接使用该容器**，无需再手动安装依赖。

macOS 宿主机上执行编译、测试、验收的命令格式为：

```bash
# 编译
docker exec rpc-ubuntu bash -c "cd /workspace && rm -rf build && bash build.sh"

# 运行单个测试
docker exec rpc-ubuntu /workspace/build/test_abstract_codec
docker exec rpc-ubuntu /workspace/build/test_tcp_buffer
docker exec rpc-ubuntu /workspace/build/test_hook

# 阶段验收
docker exec rpc-ubuntu /workspace/scripts/check_stage1.sh
```

注意：不要使用 `docker run` 临时创建新容器安装依赖，Apple Silicon 上模拟 x86-64 安装速度极慢。

### 依赖安装（仅限无预配置容器时）

#### WSL

WSL 默认按 Debian/Ubuntu 环境处理，缺少依赖时执行：

```bash
sudo apt update
sudo apt install -y build-essential cmake netcat-openbsd
```

#### Docker Linux

Docker 容器内建议使用 Debian/Ubuntu 系 Linux 环境，缺少依赖时在容器内执行：

```bash
apt update
apt install -y build-essential cmake netcat-openbsd
```

### build 目录跨环境注意事项

1. `build.sh` 生成的 `build/CMakeCache.txt` 中记录了 **宿主环境的绝对路径**（如 macOS 下的 `/Users/xxx/...` 或 Docker 容器内的 `/workspace/...`）。
2. 如果在 macOS 上执行过 `./build.sh`，再在 Docker 容器内执行 `./build.sh` 会因路径不匹配而报错：
   ```
   CMake Error: The current CMakeCache.txt directory ... is different than the directory ...
   ```
3. **切换构建环境后必须清理 `build/` 目录再重建：**
   ```bash
   rm -rf build
   ./build.sh
   ```
4. 推荐始终在 **同一个环境**（Docker 容器或 WSL）中完成编译、测试、验收，不要在两种环境之间混用。

### 编译命令

#### WSL

进入 WSL 中的项目根目录后执行：

```bash
./build.sh
```

也可以在 Windows PowerShell 内直接调用 WSL：

```powershell
wsl --cd "D:\codeproject\cpp\rpc" bash ./build.sh
```

#### Docker Linux

进入 Docker Linux 容器中的项目根目录后执行：

```bash
./build.sh
```

### 测试命令

#### WSL

先完成编译，然后在 WSL 中的项目根目录执行对应测试二进制，例如：

```bash
./build/test_tcp_echo_server
./build/test_epoll_accept
./build/test_fdevent
./build/test_reactor
./build/test_reactor_accept
./build/test_tcp_buffer
./build/test_coroutine
```

也可以在 Windows PowerShell 内直接调用 WSL 执行单个测试程序，例如：

```powershell
wsl --cd "D:\codeproject\cpp\rpc" bash -lc "./build/test_tcp_buffer"
```

#### Docker Linux

先完成编译，然后在 Docker Linux 容器中的项目根目录执行对应测试二进制，例如：

```bash
./build/test_tcp_echo_server
./build/test_epoll_accept
./build/test_fdevent
./build/test_reactor
./build/test_reactor_accept
./build/test_tcp_buffer
./build/test_coroutine
```

长期运行或需要客户端连接的测试程序，应按对应阶段文档或验收脚本要求启动和验证。

### 阶段验收命令

#### WSL

阶段验收在 WSL 中的项目根目录执行：

```bash
./scripts/check_stage1.sh
```

也可以在 Windows PowerShell 内直接调用 WSL：

```powershell
wsl --cd "D:\codeproject\cpp\rpc" bash ./scripts/check_stage1.sh
```

#### Docker Linux

阶段验收在 Docker Linux 容器中的项目根目录执行：

```bash
./scripts/check_stage1.sh
```

验收通过以脚本输出 `PASS` 为准。
