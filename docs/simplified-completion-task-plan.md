# TinyRPC 简化实现补全任务计划书

> **适用仓库**：`D:\codeproject\cpp\rpc`  
> **对照原项目**：`D:\codeproject\cpp\tinyrpc\tinyrpc`  
> **当前基线**：任务一到任务八十五已经全部完成，`scripts/check_all.sh` 是当前全量回归入口。  
> **本计划目标**：只针对已标注为“简化复刻 / 暂不复刻”的模块，规划后续从学习型实现补到更接近原 TinyRPC 完整能力的任务，不重复规划已经稳定收口的主链路。

---

## 0. 任务边界

### 0.1 本计划覆盖什么

本计划覆盖当前项目中已经能跑、但实现边界明显小于原 TinyRPC 的模块：

- 配置、日志、启动入口和运行时上下文。
- 协程 hook、协程池和协程栈内存池。
- 客户端侧 Reactor 化、响应缓存和连接复用。
- 真正异步 RPC 网络路径。
- HTTP parser、dispatcher 和连接语义。
- 生成器工程结构、`protoc` 集成和业务接口模板。
- 可选插件、观测能力、压测与最终补全矩阵。

### 0.2 本计划不覆盖什么

- 不推翻已经通过 `check_all.sh` 验收的同步 RPC、异步 RPC、HTTP、生成器主链路。
- 不为了 100% 对齐原项目文件名而大规模改目录。
- 不把所有生产级能力一次性塞进一个任务。
- 不默认引入外部服务依赖；MySQL 等插件必须保持可选。

### 0.3 执行原则

1. **一任务一提交**：每个任务独立完成、独立验证、独立提交。
2. **先补语义，再补性能**：先让行为、错误和生命周期完整，再做压测或优化。
3. **先保持现有稳定性**：每个阶段至少运行对应专项脚本，阶段结束运行 `./scripts/check_all.sh`。
4. **新增复杂入口必须有测试钩子**：透明 hook、异步网络、日志线程、生成器都必须能在测试中稳定观察。
5. **文档同步更新**：每个阶段完成后更新阶段文档、覆盖矩阵和 `docs/replica-progress.md`。

---

## 1. 简化项总览

| 模块 | 当前状态 | 主要简化点 | 补全阶段 |
|---|---|---|---|
| `comm/config` | 简化复刻 | 只读取 server/protocol/iothread/timeout/log level，缺少日志、协程、timewheel、reqId、插件配置。 | 阶段 18 |
| `comm/log` | 简化复刻 | 只有单类 Logger 和简化异步队列，缺少 RPC/App 双日志、LogEvent、周期 flush、滚动策略。 | 阶段 18 |
| `comm/start/runtime` | 简化复刻 | 已有启动入口和线程局部 request context，但全局 config/server/timer/runtime 能力不完整。 | 阶段 18 |
| `coroutinehook` | 简化复刻 | 主要使用显式 hook 函数，未提供原项目式 libc 符号级透明 hook。 | 阶段 19 |
| `coroutinepool` | 简化复刻 | 固定容量池，容量耗尽返回空，不支持内存块扩展策略。 | 阶段 19 |
| 协程栈内存池 | 简化复刻 | 固定块内存池已独立可用，但 `Coroutine` 栈仍未强制接入。 | 阶段 19 |
| `TcpClient` | 简化复刻 | 使用 `poll()` 做同步超时，未接入 Reactor、Timer、TcpConnection 响应缓存。 | 阶段 20 |
| 异步 RPC | 简化复刻 | pending/timeout/cancel 已有，但真实网络仍复用同步 `TcpClient` 路径。 | 阶段 21 |
| HTTP | 简化复刻 | 支持常见 GET/POST 和 Content-Length，缺少更完整 URL/query/header/连接语义。 | 阶段 22 |
| `generator` | 简化复刻 | 支持简单 proto service/method 和生成工程，未复刻原项目完整目录、`protoc` 流程和 interface/test_client 体系。 | 阶段 23 |
| ThreadPool / MySQL / tracing / benchmark | 暂不复刻 | 不在主链路中，但属于原项目生产外壳或完整化配套能力。 | 阶段 24 |
| 补全收口 | 未开始 | 需要新的补全覆盖矩阵、回归脚本和边界总结。 | 阶段 25 |

---

# 阶段 18：配置、日志、启动入口和运行时补全

## 阶段目标

把当前最小 `Config` / `Logger` / `Start` / `Runtime` 补成更完整的框架启动外壳。配置结构借鉴原 TinyRPC 的分组思想，但采用当前项目自己的 XML 字段和命名，不兼容原项目配置文件，也不保留旧的扁平 XML 格式。

## 阶段完成标准

- XML 迁移为当前项目自有的分组式配置结构，并能读取日志、协程、RPC、网络、时间轮和 server 核心字段。
- 旧扁平 XML 与原 TinyRPC XML 均不作为兼容目标。
- RPC 日志和 APP 日志分开记录。
- 日志事件包含时间、级别、线程、协程、文件、行号、函数名、reqId。
- 日志后台 flush、关闭、滚动策略可测试。
- 启动入口能公开 `GetConfig()`、`GetServer()`、`GetIOThreadPoolSize()`、`AddTimerTask()` 等框架级能力。
- `docs/original-coverage-matrix.md` 中 `comm/config`、`comm/log`、`comm/start`、`comm/runtime` 的状态可以从“简化复刻”推进到“已复刻核心语义”。

---

## 任务八十六：扩展 Config schema 到当前项目核心字段

**类型**：简化项补全

**状态**：已完成，提交 `d0bfe61`（`任务八十六：扩展配置核心字段`）。

### 学习目标

理解框架配置不是只服务 `TcpServer`，它还统一驱动日志、协程池、reqId、连接超时、IOThread 和时间轮。任务八十六只补齐 `Config` 内部语义字段和 getter，XML 结构迁移放到任务八十七。

### 实现目标

- 在 `Config` 中补充日志配置：
  - `log_path`
  - `log_prefix`
  - `log_max_size`
  - `log_level`
  - `app_log_level`
  - `log_sync_interval`
- 补充协程配置：
  - `cor_stack_size`
  - `cor_pool_size`
- 补充 RPC 与网络配置：
  - `req_id_len`
  - `max_connect_timeout`
  - `iothread_num`
  - `timewheel_bucket_num`
  - `timewheel_interval`
- 保留当前已有 `server_host`、`server_port`、`protocol`、`timeout` 字段。
- 所有字段都有明确默认值和 getter。
- 字段命名采用当前项目规范，不沿用原项目的历史拼写，例如 `protocal`、`inteval`、`log_sync_inteval`。

### 关键文件

- `mytinyrpc/comm/config.h`
- `mytinyrpc/comm/config.cc`
- `testcases/test_config.cc`
- `docs/stage-18.md`
- `docs/replica-progress.md`

### 测试方式

- 默认构造时所有新增字段都有合法默认值。
- 新增字段缺失时继续使用默认值。
- 非法数字字段返回失败并写入 `getLastError()`。
- log level / app log level 字符串能正确转换。

### 验收标准

```bash
./build.sh
./build/test_config
./scripts/check_all.sh
```

### 不包括

- 不在本任务接入 Logger 行为。
- 不在本任务启用 MySQL 插件。
- 不在本任务迁移 XML 文件结构。

---

## 任务八十七：迁移为当前项目分组式 XML 配置结构

**类型**：简化项补全

**状态**：已完成，提交 `8e654e4`（`任务八十七：迁移分组式配置文件`）。

### 学习目标

理解“字段完整”和“配置结构清晰”是两件事。先补齐 `Config` 语义字段，再把当前项目配置文件从扁平标签迁移为分组式 XML，使配置按模块归属组织，而不是为了兼容原项目历史格式。

### 实现目标

- 将 `conf/*.xml` 和 `generator/template/conf.xml.template` 全量迁移为当前项目自有的分组式 XML。
- `Config` 只支持新的分组式 XML，不保留旧扁平格式兼容。
- 不兼容原 TinyRPC XML 配置文件；只借鉴其按模块分组的组织方式。
- 建议分组结构：
  - `server.host`、`server.port`、`server.protocol`
  - `network.iothread_num`、`network.timeout_ms`、`network.max_connect_timeout_ms`
  - `log.path`、`log.prefix`、`log.max_size_mb`、`log.rpc_level`、`log.app_level`、`log.sync_interval_ms`
  - `coroutine.stack_size_kb`、`coroutine.pool_size`
  - `timewheel.bucket_num`、`timewheel.interval_sec`
  - `rpc.req_id_len`
- 增加结构化节点读取辅助函数，统一读取分组字段和错误提示。
- 字段单位写入字段名，避免依赖注释区分秒、毫秒、KB 或 MB。

### 关键文件

- `mytinyrpc/comm/config.cc`
- `conf/test_tinypb_server.xml`
- `conf/test_http_server.xml`
- `conf/test_start_tinypb.xml`
- `conf/test_start_http.xml`
- `conf/test_partial_server.xml`
- `conf/test_invalid_server.xml`
- `generator/template/conf.xml.template`
- `testcases/test_config.cc`
- `testcases/test_start.cc`
- `docs/stage-18.md`

### 测试方式

- 分组式 TinyPB XML 能被 `Config` 正确读取，并能启动 TinyPB server。
- 分组式 HTTP XML 能被 `Config` 正确读取，并能启动 HTTP server。
- 旧扁平 XML 不再作为有效格式；如测试需要，应明确断言失败或删除旧格式样例。
- 覆盖字段缺失、非法数字、非法协议、非法日志级别和非法节点层级。
- 生成器产出的 `conf.xml` 使用新的分组式格式。

### 验收标准

```bash
./build.sh
./build/test_config
./build/test_start
./scripts/check_generator.sh
```

### 不包括

- 不引入 TinyXML 作为硬性依赖；优先在当前轻量 parser 基础上补结构化读取辅助能力。
- 不兼容原 TinyRPC 的 `protocal`、`inteval`、`log_sync_inteval`、`msg_req_len` 等历史字段名。
- 不保留旧的 `server_addr`、根级 `protocol`、根级 `timeout` 等扁平配置格式。
- 不在本任务解析 MySQL 配置。

---

## 任务八十八：实现 RPC / APP 双日志和 LogEvent

**类型**：简化项补全

**状态**：已完成，提交 `e8b1a64`（`任务八十八：实现双日志和日志事件`）。

### 学习目标

理解框架日志和业务日志的边界：RPC 日志记录框架链路，APP 日志记录业务行为，两者可以有不同级别和不同文件。

### 实现目标

- 新增 `LogType`：`RpcLog`、`AppLog`。
- 新增 `LogEvent`，保存：
  - 时间戳。
  - 日志级别。
  - 进程 id / 线程 id。
  - 当前协程 id（没有协程时为 0）。
  - 文件、行号、函数名。
  - reqId。
  - 日志类型。
- `Logger` 支持分别设置 RPC 日志级别和 APP 日志级别。
- 新增 `AppDebugLog` / `AppInfoLog` / `AppWarnLog` / `AppErrorLog` 宏或等价函数入口。
- 日志格式在文档中固定下来。

### 关键文件

- `mytinyrpc/comm/log.h`
- `mytinyrpc/comm/log.cc`
- `mytinyrpc/comm/runtime.h`
- `testcases/test_log.cc`
- `testcases/test_runtime.cc`
- `docs/stage-18.md`

### 测试方式

- RPC 日志和 APP 日志分别按自己的 level 过滤。
- 日志行包含文件、行号、线程 id、reqId。
- request context 中存在 reqId 时，未显式传入 reqId 的日志能自动补齐。

### 验收标准

```bash
./build.sh
./build/test_log
./build/test_runtime
```

### 不包括

- 不在本任务做异步 flush。
- 不在本任务做日志滚动。

---

## 任务八十九：补齐 AsyncLogger flush、关闭和滚动策略

**类型**：简化项补全

**状态**：已完成，提交 `1cc9921`（`任务八十九：补齐异步日志生命周期`）。

### 学习目标

理解日志线程生命周期：生产者只负责入队，后台线程负责落盘、flush、按大小或日期滚动，进程退出时必须安全收尾。

### 实现目标

- `Logger::init()` 支持同时初始化 RPC 日志文件和 APP 日志文件。
- 异步模式下维护后台 flush 线程。
- 支持按 `log_sync_interval` 周期 flush。
- 支持按 `log_max_size` 进行文件滚动，滚动后生成新文件。
- `Logger::shutdown()` 保证队列全部落盘并 join 后台线程。
- `Logger::flush()` 在同步和异步模式下语义一致。

### 关键文件

- `mytinyrpc/comm/log.h`
- `mytinyrpc/comm/log.cc`
- `testcases/test_log.cc`
- `docs/stage-18.md`

### 测试方式

- 多线程并发写日志，最终行数不丢。
- `flush()` 后文件内容可见。
- `shutdown()` 后不能留下后台线程。
- 设置很小 `log_max_size` 时触发滚动。

### 验收标准

```bash
./build.sh
./build/test_log
./scripts/check_all.sh
```

### 不包括

- 不做跨进程日志锁。
- 不做压缩归档。
- 不做远程日志采集。

---

## 任务九十：启动入口和运行时上下文补全

**类型**：简化项补全

**状态**：已完成，提交 `7ff914f`（`任务九十：补全启动入口和运行时上下文`）。

### 学习目标

理解 `start` 和 `runtime` 是框架使用者看到的门面：业务 main 不应该手动拼接 config、codec、dispatcher、server、timer。

### 实现目标

- `InitConfig()` 保存全局配置对象。
- `GetConfig()` 返回当前配置。
- `StartRpcServer()` 按配置创建 TinyPB 或 HTTP server。
- `GetServer()` 返回全局 server。
- `GetIOThreadPoolSize()` 从配置读取 IOThread 数量。
- `AddTimerTask()` 将 TimerTask 投递到当前 server / reactor。
- `Runtime::RequestContext` 补齐：
  - reqId。
  - interface / method name。
  - local addr。
  - peer addr。
  - protocol type。
- `TinyPbDispatcher` 和 `HttpDispatcher` 都设置 request context。

### 关键文件

- `mytinyrpc/comm/start.h`
- `mytinyrpc/comm/start.cc`
- `mytinyrpc/comm/runtime.h`
- `mytinyrpc/comm/runtime.cc`
- `mytinyrpc/net/tinypb/tinypbdispatcher.cc`
- `mytinyrpc/net/http/httpdispatcher.cc`
- `testcases/test_start.cc`
- `testcases/test_runtime.cc`
- `docs/stage-18.md`

### 测试方式

- TinyPB 和 HTTP 都能通过 XML 启动。
- 业务处理函数中能读取完整 request context。
- 请求结束后 context 被清理。
- 多线程 context 相互隔离。

### 验收标准

```bash
./build.sh
./build/test_start
./build/test_runtime
./scripts/check_rpc_sync.sh
./scripts/check_stage12_http.sh
```

### 不包括

- 不在本任务增加配置热加载。
- 不在本任务实现 `TcpServer::stop()`。

---

# 阶段 19：协程 hook、协程池和栈内存池完整化

## 阶段目标

把当前“显式 hook + 固定容量协程池 + 独立内存池”补成更接近原 TinyRPC 的协程运行模型，同时保持测试中可控、可开关、可回退。

## 阶段完成标准

- 支持全局 hook 开关。
- 支持业务代码直接调用 `read/write/connect/accept/sleep` 时被 hook 接管。
- hook 能正确处理主协程直通、非主协程挂起、timeout、关闭开关后的系统调用直通。
- 协程池能使用配置初始化，容量耗尽时有明确扩展策略。
- 协程栈能从内存池分配并归还。

---

## 任务九十一：全局 hook 开关和透明系统调用 hook

**类型**：简化项补全

### 学习目标

理解原 TinyRPC 通过 `dlsym(RTLD_NEXT, ...)` 保存真实系统调用，再提供同名 `extern "C"` 函数实现透明 hook。

### 实现目标

- 新增 `SetHook(bool enabled)` 和 `IsHookEnabled()`。
- 使用 `dlsym(RTLD_NEXT, ...)` 获取真实系统调用：
  - `read`
  - `write`
  - `accept`
  - `connect`
  - `sleep`
- 提供透明 hook 入口。
- 主协程或 hook 关闭时直通真实系统调用。
- 现有显式 hook 函数继续保留，作为测试和内部调用入口。

### 关键文件

- `mytinyrpc/coroutine/coroutinehook.h`
- `mytinyrpc/coroutine/coroutinehook.cc`
- `testcases/test_hook.cc`
- `testcases/test_hook_socket.cc`
- `docs/coroutine-model.md`

### 测试方式

- hook 关闭时，`read/write/connect/sleep` 走真实系统调用。
- hook 开启时，非主协程直接调用系统调用也能挂起并恢复。
- 主协程中透明 hook 不误挂起。

### 验收标准

```bash
./build.sh
./build/test_hook
./build/test_hook_socket
./build/test_hook_sleep
./scripts/check_rpc_sync.sh
```

### 不包括

- 不 hook `fcntl`、`ioctl`、`getsockopt`。
- 不覆盖所有 libc 变体，例如 `recvmsg`、`sendmsg`。
- 不默认破坏已有显式 hook 测试。

---

## 任务九十二：FdEventContainer 和 hook fd 归属整理

**类型**：简化项补全

### 学习目标

理解透明 hook 需要通过 fd 找到对应 `FdEvent` 和 `Reactor`，否则系统调用无法把当前协程挂到正确事件上。

### 实现目标

- 新增或补齐 `FdEventContainer`。
- 支持按 fd 获取稳定的 `FdEvent` 对象。
- `FdEvent` 保存所属 Reactor。
- hook 路径中如果 fd 尚未绑定 Reactor，则使用当前线程 Reactor。
- fd 关闭或连接销毁时清理 container 中的注册关系。

### 关键文件

- `mytinyrpc/net/fdevent.h`
- `mytinyrpc/net/fdevent.cc`
- `mytinyrpc/net/reactor.h`
- `mytinyrpc/net/reactor.cc`
- `mytinyrpc/coroutine/coroutinehook.cc`
- `testcases/test_fdevent.cc`
- `testcases/test_hook_socket.cc`

### 测试方式

- 同一个 fd 多次获取返回同一个事件对象。
- fd 关闭后容器不持有悬空事件。
- hook 能自动绑定当前 Reactor。
- 不同 Reactor 中 fd 归属不会串线。

### 验收标准

```bash
./build.sh
./build/test_fdevent
./build/test_reactor
./build/test_hook_socket
```

### 不包括

- 不引入复杂 fd 生命周期追踪器。
- 不在本任务重写 TcpConnection。

---

## 任务九十三：CoroutinePool 配置化和扩展内存块策略

**类型**：简化项补全

### 学习目标

理解原项目协程池不是单纯固定容量数组，而是优先复用已有协程，容量不够时再从新的内存块中创建协程。

### 实现目标

- `CoroutinePool` 可通过 `Config` 的 `cor_pool_size` 和 `cor_stack_size` 初始化。
- 保留固定初始容量。
- 初始容量耗尽时按策略扩展：
  - 可选择新增一个内存块。
  - 或返回空，但必须由配置控制。
- 已使用过的协程优先复用，减少重新触碰栈内存造成的 page fault。
- 文档明确池耗尽策略。

### 关键文件

- `mytinyrpc/coroutine/coroutinepool.h`
- `mytinyrpc/coroutine/coroutinepool.cc`
- `mytinyrpc/comm/config.h`
- `testcases/test_coroutinepool.cc`
- `docs/coroutine-model.md`

### 测试方式

- 默认配置下池可正常复用。
- 容量耗尽时按配置扩展或返回空。
- 动态扩展协程归还后可再次复用。
- 归还 Suspended / Running 协程仍然失败。

### 验收标准

```bash
./build.sh
./build/test_coroutinepool
./build/test_coroutine
```

### 不包括

- 不实现复杂调度器。
- 不实现 work stealing。
- 不支持跨线程迁移协程。

---

## 任务九十四：Coroutine 栈接入 FixedMemoryPool

**类型**：简化项补全

### 学习目标

理解协程栈复用是性能优化，但一旦接入，就必须保证栈归属、生命周期和非法归还都可验证。

### 实现目标

- `Coroutine` 支持使用外部栈内存。
- `CoroutinePool` 从 `FixedMemoryPool` 获取栈块。
- 协程归还池时同步归还栈块。
- 对非池内栈保持兼容。
- `FixedMemoryPool` 记录 block 使用状态，拒绝重复归还。

### 关键文件

- `mytinyrpc/coroutine/coroutine.h`
- `mytinyrpc/coroutine/coroutine.cc`
- `mytinyrpc/coroutine/coroutinepool.cc`
- `mytinyrpc/coroutine/memory.h`
- `mytinyrpc/coroutine/memory.cc`
- `testcases/test_memory_pool.cc`
- `testcases/test_coroutinepool.cc`

### 测试方式

- 多个协程从同一个池分配不同栈块。
- 协程归还后栈块可复用。
- 非池内栈不会被误归还。
- 协程 reset 后不会保留旧任务捕获。

### 验收标准

```bash
./build.sh
./build/test_memory_pool
./build/test_coroutinepool
./build/test_coroutine
```

### 不包括

- 不使用 `mmap` 替换当前所有分配方式，除非单独验证。
- 不实现 guard page。

---

## 任务九十五：协程透明 hook 回归脚本和文档收口

**类型**：必须复刻

### 学习目标

透明 hook 最容易出现隐式副作用，必须有专项回归脚本和文档说明开关、线程、协程、fd 归属边界。

### 实现目标

- 新增 `scripts/check_coroutinehook.sh`。
- 串联运行协程、hook、socket hook、sleep hook、FdEvent、Reactor 测试。
- `docs/coroutine-model.md` 增加透明 hook 路径图。
- `docs/original-coverage-matrix.md` 更新 coroutine 状态。

### 关键文件

- `scripts/check_coroutinehook.sh`
- `docs/coroutine-model.md`
- `docs/original-coverage-matrix.md`
- `docs/replica-progress.md`

### 测试方式

```bash
./scripts/check_coroutinehook.sh
./scripts/check_all.sh
```

### 验收标准

- 脚本输出 `[coroutine-hook] PASS`。
- 文档说明 `SetHook(false)` 的测试和排障用法。
- 全量回归不因透明 hook 引入不稳定。

### 不包括

- 不做协程性能报告。

---

# 阶段 20：TcpClient Reactor 化和客户端连接语义补全

## 阶段目标

把当前 `poll()` 驱动的同步客户端补成更接近原 TinyRPC 的 Reactor/Timer/TcpConnection 客户端模型，为真正异步 RPC 网络路径做准备。

## 阶段完成标准

- `TcpClient` 内部可以使用 `TcpConnection`、`TcpBuffer`、`TinyPbCodec`。
- 同步调用的 connect / write / read 超时由 Reactor Timer 驱动。
- 客户端连接支持响应缓存和按 reqId 取响应。
- 连接复用、显式关闭、失败后重建的边界清楚。

---

## 任务九十六：TcpClient 接入客户端 TcpConnection

**类型**：简化项补全

### 学习目标

理解原项目中客户端和服务端都复用 `TcpConnection`，只是连接类型、输入输出和 dispatcher 行为不同。

### 实现目标

- `TcpConnection` 明确支持 `ClientConnection` 类型。
- 客户端连接可持有：
  - input buffer。
  - output buffer。
  - codec。
  - peer addr。
  - response map。
- `TcpClient` 通过 `TcpConnection` 编码 request、写出、读取 response。
- 保留当前直接 `sendAndRecvTinyPb()` 行为作为过渡路径或测试对照。

### 关键文件

- `mytinyrpc/net/tcpclient.h`
- `mytinyrpc/net/tcpclient.cc`
- `mytinyrpc/net/tcpconnection.h`
- `mytinyrpc/net/tcpconnection.cc`
- `testcases/test_tcp_client.cc`
- `testcases/test_connection_codec.cc`

### 测试方式

- 客户端 `TcpConnection` 能 encode request 到 output buffer。
- 读取 TinyPB response 后按 reqId 存入 response map。
- 不影响服务端 `TcpConnection` 既有测试。

### 验收标准

```bash
./build.sh
./build/test_tcp_client
./build/test_connection_codec
./scripts/check_rpc_sync.sh
```

### 不包括

- 不在本任务替换 `poll()` 超时。
- 不做异步 Channel 改造。

---

## 任务九十七：同步 TcpClient 改为 Reactor + Timer 超时模型

**类型**：简化项补全

### 学习目标

理解原项目同步 RPC 不是简单阻塞读写，而是在协程中通过 hook 挂起，由 Reactor 事件或 Timer 恢复。

### 实现目标

- `TcpClient` 获取当前线程 Reactor。
- connect 过程使用 `connectHook()` 或等价 Reactor 写事件。
- 发送和接收使用 `writeHook()` / `readHook()` 或等价 Reactor 事件。
- 超时由一次性 `TimerTask` 控制。
- 超时后设置连接 overtime flag，并恢复当前协程。
- 失败后关闭 fd，下一次调用重新创建 fd。

### 关键文件

- `mytinyrpc/net/tcpclient.cc`
- `mytinyrpc/net/tcpconnection.cc`
- `mytinyrpc/coroutine/coroutinehook.cc`
- `mytinyrpc/net/timer.h`
- `testcases/test_tcp_client.cc`
- `testcases/test_tinypb_rpc_channel.cc`

### 测试方式

- connect 超时。
- read 超时。
- write 超时或对端关闭。
- 超时后下一次请求能重新连接成功。

### 验收标准

```bash
./build.sh
./build/test_tcp_client
./build/test_tinypb_rpc_channel
./scripts/check_rpc_sync.sh
```

### 不包括

- 不做真正异步多请求发送队列。
- 不做连接池。

---

## 任务九十八：客户端 response map 和迟到响应处理

**类型**：简化项补全

### 学习目标

理解同步单请求模型可以直接等待当前响应，但客户端连接复用后，仍需要明确“不匹配响应、迟到响应、未知 reqId”的处理策略。

### 实现目标

- `TcpConnection` 客户端侧维护 `reqId -> TinyPbStruct` response map。
- `getResPackageData(reqId)` 成功后删除 map 中响应。
- 未知 reqId 记录日志并保留或丢弃，策略写入文档。
- 同步 Channel 收到非当前 reqId 时不污染业务 response。

### 关键文件

- `mytinyrpc/net/tcpconnection.h`
- `mytinyrpc/net/tcpconnection.cc`
- `mytinyrpc/net/tcpclient.cc`
- `mytinyrpc/net/tinypb/tinypbrpcchannel.cc`
- `testcases/test_tcp_client.cc`
- `testcases/test_tinypb_rpc_channel.cc`

### 测试方式

- mock server 先返回错误 reqId，再返回正确 reqId。
- 未知响应不会使当前调用成功。
- 正确响应被取出后 map 清理。

### 验收标准

```bash
./build.sh
./build/test_tcp_client
./build/test_tinypb_rpc_channel
```

### 不包括

- 不在同步客户端支持多个并发 in-flight 请求。
- 不把异步 pending map 移回同步 Channel。

---

## 任务九十九：连接复用、显式关闭和失败重建策略

**类型**：简化项补全

### 学习目标

理解客户端 fd 生命周期：复用能减少开销，但错误、超时、对端关闭后必须进入可解释的重建路径。

### 实现目标

- `TcpClient` 支持连接复用开关。
- 默认同步调用可复用已连接 fd。
- `closeConnection()` 后下一次调用重建连接。
- 网络失败、超时、协议错误后按策略关闭或保留连接。
- 文档列出每类错误后的连接状态。

### 关键文件

- `mytinyrpc/net/tcpclient.h`
- `mytinyrpc/net/tcpclient.cc`
- `docs/stage-20.md`
- `testcases/test_tcp_client.cc`

### 测试方式

- 两次请求复用同一个 fd。
- 显式 close 后 fd 变化。
- 对端关闭后下一次请求可重连。
- 协议错误不会误用脏 buffer。

### 验收标准

```bash
./build.sh
./build/test_tcp_client
./scripts/check_rpc_sync.sh
```

### 不包括

- 不做多目标地址连接池。
- 不做负载均衡。

---

## 任务一百：客户端 Reactor 化回归脚本

**类型**：必须复刻

### 学习目标

把客户端网络模型变化纳入稳定回归，防止后续异步网络改造时破坏同步 RPC。

### 实现目标

- 新增 `scripts/check_rpc_client_reactor.sh`。
- 覆盖 TcpClient、TinyPbRpcChannel、同步 RPC 端到端、连接失败与超时用例。
- `docs/stage-20.md` 汇总客户端连接状态机。

### 关键文件

- `scripts/check_rpc_client_reactor.sh`
- `docs/stage-20.md`
- `docs/replica-progress.md`

### 测试方式

```bash
./scripts/check_rpc_client_reactor.sh
./scripts/check_rpc_sync.sh
./scripts/check_all.sh
```

### 验收标准

- 脚本输出 `[rpc-client-reactor] PASS`。
- 同步 RPC 仍保持稳定。

### 不包括

- 不做异步 RPC 网络路径替换。

---

# 阶段 21：真正异步 RPC 网络路径

## 阶段目标

把当前“异步接口 + IOThread 中执行同步 TcpClient”的实现，升级为真正由 Reactor 驱动的异步请求发送、响应读取、pending 匹配、timeout 和 cancel。

## 阶段完成标准

- 异步 Channel 不再依赖同步 `TcpClient::sendAndRecvTinyPb()` 作为默认路径。
- IOThread 中维护连接、发送队列和读取循环。
- 多个 in-flight 请求可共用连接。
- 响应按 reqId 匹配 pending。
- timeout / cancel 能清理 pending，并避免二次回调。
- 异步端到端脚本使用真实 `TcpServer`。

---

## 任务一百零一：异步 Channel 网络会话对象

**类型**：简化项补全

### 学习目标

理解真正异步 RPC 需要一个长生命周期网络会话，而不是每次调用临时创建同步客户端。

### 实现目标

- 新增 `AsyncClientSession` 或等价内部结构。
- 保存 peer addr、fd、TcpConnection、codec、所属 IOThread/Reactor。
- Channel 构造时初始化会话或懒加载会话。
- 会话负责连接建立和重连。
- Channel 只负责构造 request、注册 pending、投递发送任务。

### 关键文件

- `mytinyrpc/net/tinypb/tinypbrpcasyncchannel.h`
- `mytinyrpc/net/tinypb/tinypbrpcasyncchannel.cc`
- `mytinyrpc/net/tcpclient.h`
- `mytinyrpc/net/tcpconnection.h`
- `testcases/test_tinypb_rpc_async_channel.cc`

### 测试方式

- Channel 创建后 IOThread 启动。
- 多次调用复用同一个 session。
- session stop 后 fd 和 pending 清理。

### 验收标准

```bash
./build.sh
./build/test_tinypb_rpc_async_channel
```

### 不包括

- 不在本任务实现完整发送队列。
- 不在本任务做真实服务端端到端。

---

## 任务一百零二：异步发送队列和 Reactor 写事件

**类型**：简化项补全

### 学习目标

理解异步请求发送不是直接阻塞写 socket，而是入队后由 Reactor 在 fd 可写时逐步 flush。

### 实现目标

- 为异步 session 增加发送队列。
- 每个 request encode 后追加到 output buffer。
- 注册 EPOLLOUT 写事件。
- 写完当前 buffer 后取消不必要的写监听。
- 写失败时只失败相关 pending，必要时关闭连接并失败全部 pending。

### 关键文件

- `mytinyrpc/net/tinypb/tinypbrpcasyncchannel.cc`
- `mytinyrpc/net/tcpconnection.cc`
- `mytinyrpc/net/reactor.cc`
- `testcases/test_tinypb_rpc_async_channel.cc`

### 测试方式

- 连续发起多个异步请求，调用线程不阻塞。
- 大 payload 可分多次写出。
- 写失败清理 pending 并执行 closure。

### 验收标准

```bash
./build.sh
./build/test_tinypb_rpc_async_channel
./build/test_connection_codec
```

### 不包括

- 不做批量合并优化。
- 不做连接池。

---

## 任务一百零三：异步读取循环和乱序响应匹配

**类型**：简化项补全

### 学习目标

理解异步读循环必须持续解析 TCP 字节流，并把每个完整 TinyPB response 交给 pending map。

### 实现目标

- 注册 EPOLLIN 读事件。
- 读到数据后追加 input buffer。
- 循环 decode 多个完整 TinyPB response。
- 每个 response 调用 `handleTinyPbResponse()`。
- 支持乱序响应。
- 未知 reqId 响应记录日志并丢弃。

### 关键文件

- `mytinyrpc/net/tinypb/tinypbrpcasyncchannel.cc`
- `mytinyrpc/net/tcpconnection.cc`
- `mytinyrpc/net/tinypb/tinypbcodec.cc`
- `testcases/test_tinypb_rpc_async_channel.cc`

### 测试方式

- mock server 一次返回多个 response。
- mock server 乱序返回 response。
- mock server 返回未知 reqId response。
- 粘包和半包都正确处理。

### 验收标准

```bash
./build.sh
./build/test_tinypb_rpc_async_channel
./build/test_tinypb_codec
```

### 不包括

- 不做流控。
- 不做背压策略。

---

## 任务一百零四：异步 timeout / cancel 打断网络状态

**类型**：简化项补全

### 学习目标

理解 timeout/cancel 不只是删除 pending，还要和正在连接、正在写、正在读的网络状态协调。

### 实现目标

- timeout 到期后删除 pending，并从发送队列中移除未发送 request。
- cancel 后如果 request 尚未发送，直接清理。
- cancel 后如果 request 已发送，标记 canceled，迟到响应丢弃。
- 所有完成路径统一走一次性仲裁，避免二次回调。
- session 关闭时失败全部 pending。

### 关键文件

- `mytinyrpc/net/tinypb/tinypbrpcasyncchannel.cc`
- `mytinyrpc/net/timer.h`
- `mytinyrpc/net/reactor.h`
- `testcases/test_tinypb_rpc_async_channel.cc`

### 测试方式

- 发送前 cancel。
- 发送后 response 前 cancel。
- timeout 后迟到 response 不二次回调。
- session close 时全部 pending 收到失败。

### 验收标准

```bash
./build.sh
./build/test_tinypb_rpc_async_channel
./build/test_timer
```

### 不包括

- 不实现重试策略。
- 不实现业务级取消传播协议。

---

## 任务一百零五：真实 TcpServer 异步 RPC 端到端验收

**类型**：必须复刻

### 学习目标

把异步 RPC 从 mock server 验证升级为真实 `TcpServer` + `TinyPbDispatcher` + Protobuf Service 端到端验证。

### 实现目标

- 新增或扩展异步客户端验收程序。
- 脚本启动真实 TinyPB server。
- 异步客户端并发发起多个 Stub 调用。
- 覆盖成功、服务端错误、timeout、cancel。
- `scripts/check_rpc_async.sh` 默认使用真实服务端路径。

### 关键文件

- `testcases/test_tinypb_async_client.cc`
- `testcases/test_tinypb_server_client.cc`
- `scripts/check_rpc_async.sh`
- `docs/stage-21.md`
- `docs/original-coverage-matrix.md`

### 测试方式

```bash
./build.sh
./scripts/check_rpc_async.sh
./scripts/check_all.sh
```

### 验收标准

- `check_rpc_async.sh` 输出 `[rpc-async] PASS`。
- 异步网络默认路径不再依赖同步 `TcpClient::sendAndRecvTinyPb()`。
- 文档明确 closure 执行线程和 pending 生命周期。

### 不包括

- 不写性能报告。
- 不实现连接池和负载均衡。

---

# 阶段 22：HTTP 协议栈补全

## 阶段目标

在不追求 HTTPS / HTTP2 / chunked 的前提下，把当前 HTTP 最小闭环补成原项目风格更完整、边界更清楚的 HTTP/1.0 / HTTP/1.1 request-response 支持。

## 阶段完成标准

- request line 支持 origin-form 和 absolute-form URL。
- query string 被解析到 map。
- header 处理大小写更稳健。
- response 生成包含合理默认 header。
- servlet 分发支持 root/default 行为。
- HTTP 连接关闭 / keep-alive 策略明确。

---

## 任务一百零六：HTTP request line、URL 和 query 解析补全

**类型**：简化项补全

### 学习目标

理解 HTTP parser 的核心不是“找到空格”，而是要稳定解析 method、target、version、path、query。

### 实现目标

- 支持 `GET /path?x=1 HTTP/1.1`。
- 支持 `GET http://host/path?x=1 HTTP/1.1`。
- 支持 root path `/`。
- 支持空 query、重复 query key 的确定策略。
- method 和 version 校验错误时可恢复。

### 关键文件

- `mytinyrpc/net/http/httprequest.h`
- `mytinyrpc/net/http/httprequest.cc`
- `mytinyrpc/net/http/httpcodec.cc`
- `testcases/test_http_codec.cc`

### 测试方式

- origin-form URL。
- absolute-form URL。
- root path。
- query map。
- 非法 request line。

### 验收标准

```bash
./build.sh
./build/test_http_codec
```

### 不包括

- 不支持 CONNECT authority-form。
- 不支持 HTTP/2 pseudo header。

---

## 任务一百零七：HTTP header 和 body 解析增强

**类型**：简化项补全

### 学习目标

理解 header 需要处理大小写、空格、重复字段和 Content-Length 边界，body 需要和半包处理配合。

### 实现目标

- header name 查询大小写不敏感。
- header value 去除首尾空格。
- Content-Length 非数字时返回解析失败。
- Content-Length 超过限制时拒绝。
- POST body 半包补齐后成功解析。
- 支持没有 body 的 GET/POST。

### 关键文件

- `mytinyrpc/net/http/httprequest.h`
- `mytinyrpc/net/http/httpcodec.cc`
- `testcases/test_http_codec.cc`

### 测试方式

- `content-length` 小写。
- header 冒号后有空格。
- 非法 Content-Length。
- body 半包。
- body 长度超限。

### 验收标准

```bash
./build.sh
./build/test_http_codec
./scripts/check_stage12_http.sh
```

### 不包括

- 不支持 chunked body。
- 不支持 multipart。

---

## 任务一百零八：HTTP response 默认 header 和错误响应

**类型**：简化项补全

### 学习目标

理解 response encode 需要对客户端友好：状态行、Content-Length、Content-Type、Connection 等默认值要稳定。

### 实现目标

- `HttpResponse` 支持默认 version、status、reason phrase。
- encode 时自动补齐或修正 `Content-Length`。
- 缺少 `Content-Type` 时给出默认值。
- 根据连接策略写入 `Connection: close` 或 `keep-alive`。
- 统一生成 400、404、500 错误响应。

### 关键文件

- `mytinyrpc/net/http/httpresponse.h`
- `mytinyrpc/net/http/httpresponse.cc`
- `mytinyrpc/net/http/httpdefine.h`
- `mytinyrpc/net/http/httpdefine.cc`
- `mytinyrpc/net/http/httpcodec.cc`
- `testcases/test_httpdefine.cc`
- `testcases/test_http_codec.cc`

### 测试方式

- 200 response 默认 header。
- 404 response。
- 500 response。
- Content-Length 修正。
- Connection header 策略。

### 验收标准

```bash
./build.sh
./build/test_httpdefine
./build/test_http_codec
```

### 不包括

- 不支持 gzip。
- 不支持 streaming response。

---

## 任务一百零九：HTTP Servlet 分发补全

**类型**：简化项补全

### 学习目标

理解 HTTP dispatcher 除了 exact path，还要处理 root、默认 servlet、重复注册和业务异常边界。

### 实现目标

- 支持注册 `/` root servlet。
- 支持默认 NotFound servlet。
- 重复注册返回失败并保留旧 servlet。
- servlet 执行异常或失败时返回 500。
- request context 中写入 path、method、reqId。

### 关键文件

- `mytinyrpc/net/http/httpservlet.h`
- `mytinyrpc/net/http/httpservlet.cc`
- `mytinyrpc/net/http/httpdispatcher.h`
- `mytinyrpc/net/http/httpdispatcher.cc`
- `mytinyrpc/comm/runtime.h`
- `testcases/test_http_dispatcher.cc`
- `testcases/test_runtime.cc`

### 测试方式

- root path。
- unknown path。
- duplicate path。
- servlet 返回 500。
- runtime context。

### 验收标准

```bash
./build.sh
./build/test_http_dispatcher
./build/test_runtime
```

### 不包括

- 不实现正则路由。
- 不实现 path parameter。

---

## 任务一百一十：HTTP 连接语义和脚本收口

**类型**：必须复刻

### 学习目标

把 HTTP parser/response/dispatcher 的增强放回真实 `TcpServer` 中验证，避免只停留在 codec 单测。

### 实现目标

- `TcpConnection` 根据 HTTP `Connection` header 决定是否关闭连接。
- 至少支持同一连接连续请求的明确策略：
  - 若当前决定不支持 keep-alive，则统一 `Connection: close` 并关闭。
  - 若支持 keep-alive，则测试连续两个请求。
- `scripts/check_stage12_http.sh` 增加 query、404、500、POST body 验收。
- `docs/stage-22.md` 汇总 HTTP 当前支持矩阵。

### 关键文件

- `mytinyrpc/net/tcpconnection.cc`
- `mytinyrpc/net/http/httpcodec.cc`
- `testcases/test_http_server.cc`
- `scripts/check_stage12_http.sh`
- `docs/stage-22.md`
- `docs/original-coverage-matrix.md`

### 测试方式

```bash
./build.sh
./scripts/check_stage12_http.sh
./scripts/check_all.sh
```

### 验收标准

- HTTP 脚本输出 `[stage12-http] PASS`。
- 文档明确不支持 HTTPS / HTTP2 / chunked。

### 不包括

- 不支持 TLS。
- 不支持 HTTP/2。

---

# 阶段 23：生成器完整化

## 阶段目标

把当前简化生成器补成更接近原项目的“可生成业务工程”的工具：目录更完整、`protoc` 流程更完整、业务 interface/service/test_client 模板更完整，同时保留当前 CMake 生成工程可验收的优点。

## 阶段完成标准

- 生成目录包含 `bin`、`conf`、`log`、`lib`、`obj`、`service`、`interface`、`pb`、`test_client` 等结构。
- 生成器能调用 `protoc` 生成 `.pb.h/.pb.cc` 或使用 descriptor-set 获取 service/method。
- 为每个 rpc method 生成独立 interface 类。
- 生成 service 实现类统一调用 interface。
- 生成 test client。
- 生成工程可构建、启动、调用、关闭。

---

## 任务一百一十一：生成原项目风格工程目录

**类型**：简化项补全

### 学习目标

理解原生成器的产物不是几个源码文件，而是一个带运行目录、日志目录、配置目录和测试客户端的工程骨架。

### 实现目标

- `tinyrpc_generator.py` 新增 `--layout full`。
- full layout 生成：
  - `bin/`
  - `conf/`
  - `log/`
  - `lib/`
  - `obj/`
  - `<project>/service/`
  - `<project>/interface/`
  - `<project>/pb/`
  - `<project>/comm/`
  - `test_client/`
- 保留当前简单 layout，避免破坏已有示例。

### 关键文件

- `generator/tinyrpc_generator.py`
- `generator/template/*`
- `scripts/check_generator.sh`
- `docs/stage-23.md`

### 测试方式

- 默认 layout 仍生成当前工程。
- `--layout full` 生成完整目录清单。
- 重复生成时已存在文件的覆盖策略明确。

### 验收标准

```bash
./scripts/check_generator.sh
```

### 不包括

- 不在本任务调用 `protoc`。

---

## 任务一百一十二：生成器集成 protoc

**类型**：简化项补全

### 学习目标

理解生成业务工程时，Protobuf 代码本身也属于生成产物，不能只复制 `.proto`。

### 实现目标

- 生成器检查 `protoc` 是否可用。
- 将输入 proto 复制到 `pb/` 目录。
- 调用 `protoc --cpp_out` 生成 `.pb.h/.pb.cc`。
- 支持输出 descriptor-set，供后续解析 service/method。
- protoc 失败时输出明确错误并返回非零。

### 关键文件

- `generator/tinyrpc_generator.py`
- `scripts/check_generator.sh`
- `docs/stage-23.md`

### 测试方式

- 使用 `test_tinypb_server.proto` 生成 pb 文件。
- protoc 不存在时提示明确。
- 非法 proto 时返回失败。

### 验收标准

```bash
./scripts/check_generator.sh
```

### 不包括

- 不支持多语言生成。

---

## 任务一百一十三：用 descriptor-set 解析 service/method

**类型**：简化项补全

### 学习目标

原项目通过解析 `.pb.h` 字符串提取 service/method，当前补全优先使用 Protobuf descriptor，减少脆弱字符串解析。

### 实现目标

- 生成器读取 descriptor-set。
- 提取 package、service、method、request type、response type。
- 支持一个 proto 多个 service。
- 支持按 `--service` 选择目标 service。
- 当 descriptor-set 不可用时保留当前简单 parser fallback。

### 关键文件

- `generator/tinyrpc_generator.py`
- `scripts/check_generator.sh`
- `testcases/test_tinypb_server.proto`

### 测试方式

- 单 service。
- 多 service。
- service 不存在。
- method 列表正确。
- request/response type 正确。

### 验收标准

```bash
./scripts/check_generator.sh
```

### 不包括

- 不支持 streaming RPC。
- 不支持 proto2 特殊语义。

---

## 任务一百一十四：生成 interface/service/business exception 模板

**类型**：简化项补全

### 学习目标

理解原项目生成器将业务方法拆成 service 层和 interface 层，service 负责 Protobuf 接口适配，interface 负责业务实现。

### 实现目标

- 生成 `business_exception.h`。
- 生成 interface base。
- 每个 rpc method 生成一个 interface 类。
- service 实现类中调用对应 interface。
- 生成代码包含清晰注释，说明 request/response 参数。
- 生成代码符合当前编码规范。

### 关键文件

- `generator/template/business_exception.h.template`
- `generator/template/interface_base.h.template`
- `generator/template/interface_base.cc.template`
- `generator/template/interface.h.template`
- `generator/template/interface.cc.template`
- `generator/template/server.h.template`
- `generator/template/server.cc.template`
- `generator/tinyrpc_generator.py`
- `scripts/check_generator.sh`

### 测试方式

- 生成一个方法。
- 生成多个方法。
- 编译生成的 interface/service。
- 检查 include 路径和 namespace。

### 验收标准

```bash
./scripts/check_generator.sh
```

### 不包括

- 不生成真实业务逻辑。

---

## 任务一百一十五：完整生成工程端到端验收

**类型**：必须复刻

### 学习目标

验证 full layout 不是静态文件清单，而是真的可构建、启动和调用。

### 实现目标

- full layout 同时生成 CMakeLists 或 makefile。
- 生成 run / shutdown 脚本。
- 生成 test client。
- `scripts/check_generator_project.sh` 增加 full layout 模式。
- 构建生成工程。
- 启动生成 server。
- 运行生成 test client。
- 关闭 server。

### 关键文件

- `generator/tinyrpc_generator.py`
- `generator/template/CMakeLists.txt.template`
- `generator/template/makefile.template`
- `generator/template/run.sh.template`
- `generator/template/shutdown.sh.template`
- `generator/template/test_tinyrpc_client.cc.template`
- `scripts/check_generator_project.sh`
- `docs/stage-23.md`
- `docs/original-coverage-matrix.md`

### 测试方式

```bash
./scripts/check_generator_project.sh
./scripts/check_all.sh
```

### 验收标准

- 生成工程脚本输出 `[generator] PASS`。
- 当前 simple layout 和 full layout 都可验收。

### 不包括

- 不生成 IDE 工程。
- 不做交互式项目向导。

---

# 阶段 24：可选插件、观测和性能边界

## 阶段目标

补齐原 TinyRPC 中当前暂不复刻、但与完整项目外壳相关的能力。该阶段必须保持可选，不允许让普通构建依赖外部 MySQL 或压测工具。

## 阶段完成标准

- 通用 ThreadPool 可用。
- MySQL 插件有可选编译开关和配置解析。
- tracing / request context 能贯穿 TinyPB、HTTP、日志。
- 有基础 benchmark 和资源泄漏检查脚本。

---

## 任务一百一十六：通用 ThreadPool 工具

**类型**：可选补全

### 学习目标

理解原项目 `comm/thread_pool` 是框架工具，不等同于 IOThreadPool。它适合普通后台任务，而 IOThreadPool 负责 Reactor 网络事件。

### 实现目标

- 新增 `ThreadPool`。
- 支持固定线程数。
- 支持 `addTask()`。
- 支持 `start()`、`stop()`、析构 join。
- 支持 stop 后拒绝新任务。

### 关键文件

- `mytinyrpc/comm/thread_pool.h`
- `mytinyrpc/comm/thread_pool.cc`
- `testcases/test_thread_pool.cc`
- `CMakeLists.txt`

### 测试方式

- 多任务并发执行。
- stop 后线程退出。
- stop 后 addTask 失败。
- 析构不泄漏线程。

### 验收标准

```bash
./build.sh
./build/test_thread_pool
```

### 不包括

- 不替换 IOThreadPool。
- 不实现任务优先级。

---

## 任务一百一十七：MySQL 插件配置和可选编译骨架

**类型**：可选补全

### 学习目标

理解插件能力必须可选：普通 RPC/HTTP 构建不应因为机器没有 MySQL 开发库而失败。

### 实现目标

- 新增 `DECLARE_MYSQL_PLUGIN` 或 CMake option。
- option 关闭时提供清晰的 no-op 边界。
- option 开启时编译 MySQL wrapper。
- `Config` 能解析 MySQL 连接配置。
- `MySQLInstanceFactory` 提供线程局部实例获取入口。

### 关键文件

- `mytinyrpc/comm/mysql_instance.h`
- `mytinyrpc/comm/mysql_instance.cc`
- `mytinyrpc/comm/config.h`
- `mytinyrpc/comm/config.cc`
- `CMakeLists.txt`
- `docs/stage-24.md`

### 测试方式

- 默认构建不开启 MySQL，`check_all.sh` 不受影响。
- 开启 option 但无库时 CMake 给出明确提示。
- 只做配置解析单测，不要求本机有 MySQL server。

### 验收标准

```bash
./build.sh
./scripts/check_all.sh
```

可选环境下：

```bash
cmake -S . -B build -DMYTINYRPC_ENABLE_MYSQL=ON
```

### 不包括

- 不在默认回归中连接真实 MySQL。
- 不实现连接池。

---

## 任务一百一十八：Tracing / request context 补全

**类型**：可选补全

### 学习目标

理解完整 RPC 框架需要能回答“这个日志属于哪个请求、哪个接口、哪个对端、哪个线程/协程”。

### 实现目标

- request context 贯穿：
  - TinyPB request。
  - HTTP request。
  - 同步客户端。
  - 异步客户端。
  - 日志系统。
- 每次请求生成 trace id 或复用 reqId。
- 日志中自动带 trace/reqId/interface/peer。
- 文档说明 tracing 当前是轻量实现，不是完整分布式追踪系统。

### 关键文件

- `mytinyrpc/comm/runtime.h`
- `mytinyrpc/comm/runtime.cc`
- `mytinyrpc/comm/log.cc`
- `mytinyrpc/net/tinypb/tinypbdispatcher.cc`
- `mytinyrpc/net/http/httpdispatcher.cc`
- `mytinyrpc/net/tinypb/tinypbrpcchannel.cc`
- `mytinyrpc/net/tinypb/tinypbrpcasyncchannel.cc`
- `testcases/test_runtime.cc`
- `testcases/test_log.cc`

### 测试方式

- TinyPB server 日志带 reqId。
- HTTP server 日志带 path/method。
- 同步 client 日志带 peer addr。
- 异步 callback 中可读取或观察请求号。

### 验收标准

```bash
./build.sh
./build/test_runtime
./build/test_log
./scripts/check_rpc_sync.sh
./scripts/check_rpc_async.sh
./scripts/check_stage12_http.sh
```

### 不包括

- 不实现 OpenTelemetry。
- 不做跨进程 trace 传播协议。

---

## 任务一百一十九：基础 benchmark 和资源泄漏检查

**类型**：可选补全

### 学习目标

理解完整化后需要知道性能和资源是否明显退化，但本项目仍以学习型可解释为主。

### 实现目标

- 新增基础 benchmark 程序：
  - TinyPB sync 单请求延迟。
  - TinyPB async 并发请求吞吐。
  - HTTP GET 简单吞吐。
- 新增资源检查脚本：
  - 进程退出后无残留 server。
  - fd 数量不持续增长。
  - 日志线程可退出。
- benchmark 输出只作为参考，不作为严格性能门禁。

### 关键文件

- `testcases/benchmark_tinypb_sync.cc`
- `testcases/benchmark_tinypb_async.cc`
- `testcases/benchmark_http.cc`
- `scripts/check_resource_lifetime.sh`
- `docs/stage-24.md`

### 测试方式

```bash
./build.sh
./scripts/check_resource_lifetime.sh
```

### 验收标准

- 资源检查脚本输出 `[resource] PASS`。
- benchmark 可运行并输出基础统计。

### 不包括

- 不做商业级压测。
- 不引入复杂压测依赖。

---

# 阶段 25：补全计划收口

## 阶段目标

在阶段 18 到阶段 24 完成后，更新覆盖矩阵、文档和一键回归，使“简化实现补全”有明确终点。

## 阶段完成标准

- 有新的补全覆盖矩阵。
- 有完整补全回归脚本。
- README 和学习总结说明哪些能力已从简化补全，哪些仍保留为边界。

---

## 任务一百二十：更新原 TinyRPC 覆盖矩阵

**类型**：必须复刻

### 学习目标

用矩阵而不是口头描述收口，避免后续不知道哪些简化项已经补完。

### 实现目标

- 更新 `docs/original-coverage-matrix.md`。
- 新增“补全任务编号”列。
- 对每个模块重新标注：
  - 已复刻。
  - 已复刻核心语义。
  - 保留简化。
  - 可选未启用。
- 每项写明验证脚本。

### 关键文件

- `docs/original-coverage-matrix.md`
- `docs/simplified-completion-task-plan.md`
- `docs/replica-progress.md`

### 测试方式

- 人工核对矩阵与脚本入口一致。

### 验收标准

- 矩阵能直接回答“哪些简化实现已经补全”。

### 不包括

- 不改代码。

---

## 任务一百二十一：新增完整补全回归脚本

**类型**：必须复刻

### 学习目标

把所有补全阶段的验证入口汇总，形成比 `check_all.sh` 更偏“完整化能力”的回归脚本。

### 实现目标

- 新增 `scripts/check_full_completion.sh`。
- 串联：
  - `check_all.sh`
  - `check_coroutinehook.sh`
  - `check_rpc_client_reactor.sh`
  - `check_rpc_async.sh`
  - `check_stage12_http.sh`
  - `check_generator_project.sh`
  - `check_resource_lifetime.sh`
- 输出统一 PASS/FAIL。

### 关键文件

- `scripts/check_full_completion.sh`
- `scripts/check_all.ps1`
- `README.md`

### 测试方式

```bash
./scripts/check_full_completion.sh
```

### 验收标准

- 脚本输出 `[full-completion] PASS`。

### 不包括

- 不强绑定云 CI。

---

## 任务一百二十二：README 和学习总结补全

**类型**：必须复刻

### 学习目标

让项目入口从“学习型主链路完成”升级为“主链路完成 + 简化项补全说明完整”。

### 实现目标

- README 新增“补全能力”章节。
- `docs/learning-summary.md` 新增阶段 18 到阶段 25 总结。
- examples 文档说明哪些示例走完整化路径。
- 明确仍然不追求商业级生产发布。

### 关键文件

- `README.md`
- `docs/learning-summary.md`
- `examples/*/README.md`

### 测试方式

- 人工检查文档链接。
- 运行最终回归脚本。

### 验收标准

```bash
./scripts/check_full_completion.sh
```

### 不包括

- 不写商业用户手册。

---

## 任务一百二十三：最终边界审计和收口提交

**类型**：必须复刻

### 学习目标

为第二轮补全计划建立明确终点：哪些已经补完，哪些明确保留边界，哪些未来需要新计划。

### 实现目标

- 审计 `rg "简化|暂不|TODO|placeholder|后续"` 的剩余项。
- 分类剩余项：
  - 已有计划但未执行。
  - 明确不做。
  - 需要新计划。
- 更新 `docs/replica-progress.md`。
- 更新 `docs/original-coverage-matrix.md`。
- 确认 `git status` 只包含本任务相关文件。

### 关键文件

- `docs/replica-progress.md`
- `docs/original-coverage-matrix.md`
- `docs/learning-summary.md`

### 测试方式

```bash
rg -n "简化|暂不|TODO|placeholder|后续" docs mytinyrpc generator testcases
./scripts/check_full_completion.sh
git status --short
```

### 验收标准

- 第二轮补全计划有明确完成记录。
- 剩余边界都有解释，不是遗漏。

### 不包括

- 不在本任务继续补代码。

---

# 2. 建议执行顺序

## 2.1 最推荐的下一步

**任务九十一：全局 hook 开关和透明系统调用 hook。**

理由：

- 阶段 18 已经补齐配置、日志、启动入口和 request context。
- 阶段 19 会继续把当前显式 hook 推进为更接近原 TinyRPC 的透明系统调用 hook。
- 任务九十一是阶段 19 的第一个任务，范围集中在 hook 开关和系统调用入口。

## 2.2 阶段依赖

| 阶段 | 前置依赖 | 可否跳过 |
|---|---|---|
| 阶段 18 | 当前任务八十五完成状态 | 不建议跳过 |
| 阶段 19 | 阶段 18 的协程配置字段 | 可延后 |
| 阶段 20 | 阶段 19 的 hook 稳定性更好，但不是硬依赖 | 可和阶段 22/23 调换 |
| 阶段 21 | 阶段 20 客户端 Reactor 化 | 不建议提前 |
| 阶段 22 | 当前 HTTP 阶段 12 | 可独立推进 |
| 阶段 23 | 当前生成器阶段 16 | 可独立推进 |
| 阶段 24 | 阶段 18、19、21 的上下文能力 | 可选 |
| 阶段 25 | 阶段 18 到 24 完成 | 最后执行 |

## 2.3 每次任务完成后的固定动作

1. 运行任务专项测试。
2. 运行相关阶段回归脚本。
3. 更新对应阶段文档。
4. 更新 `docs/replica-progress.md`。
5. 必要时更新 `docs/original-coverage-matrix.md`。
6. 使用中文提交。

---

# 3. 当前结论

当前项目已经完成 TinyRPC 学习型复刻主线；接下来不应该继续添加零散功能，而应该按本计划集中补齐“之前为了学习节奏而简化”的部分。

最合理的起点是：

```text
任务九十一：全局 hook 开关和透明系统调用 hook
```

阶段 18 已完成，配置、日志、启动入口和运行时已经成为后续透明 hook、客户端 Reactor 化、真正异步 RPC 和生成器完整化的共同地基。下一步可以进入阶段 19。
