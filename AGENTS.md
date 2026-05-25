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

## 提交规范

1. `git commit` 的提交记录必须使用中文。
2. 提交标题与正文统一使用 `UTF-8` 编码，默认按 `UTF-8，无 BOM` 处理。

## Markdown Mermaid 规范

1. Mermaid 节点或时序图消息里需要换行时，统一使用 `<br/>`。
2. 不要写字面量 `\n`，避免在当前 Markdown 预览里无法正常换行。

## 文档索引

### 总入口

- [阶段 1：阻塞式 TCP Echo 服务器](docs/stage-1.md)
- [编码规范](docs/40-工程规范/编码规范.md)


## 构建要求

本项目以 Linux 作为目标运行环境。Windows 下必须通过 WSL 进行构建、运行和验收，不使用 Windows 原生工具链直接构建。

### 依赖安装

WSL 默认按 Debian/Ubuntu 环境处理，缺少依赖时执行：

```bash
sudo apt update
sudo apt install -y build-essential cmake netcat-openbsd
```

### 构建命令

在 Linux/WSL 内执行：

```bash
./build.sh
```

在 Windows PowerShell 内执行：

```powershell
.\build.ps1
```

也可以在 Windows PowerShell 内直接调用 WSL：

```powershell
wsl --cd "D:\codeproject\cpp\rpc" bash ./build.sh
```

### 验收命令

阶段验收统一在 WSL 内执行：

```powershell
wsl --cd "D:\codeproject\cpp\rpc" bash ./scripts/check_stage1.sh
```
