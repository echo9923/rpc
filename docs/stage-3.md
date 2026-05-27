# 阶段 3：协程与 IO Hook

## 目标

阶段 3 在阶段 2（非阻塞 IO + Reactor）的基础上引入协程，将网络 IO 从回调驱动转变为协程驱动，使业务代码可以按同步风格编写。

## 当前进度

| 任务 | 状态 | 说明 |
|------|------|------|
| 任务二十：抽象最小协程对象与 FdEvent 挂载点 | 已完成 | 最小 `Coroutine` 类（创建/恢复/让出/完成）；`FdEvent` 挂载 `Coroutine*`。 |
| 任务二十一：引入 read_hook/write_hook 的最小雏形 | 已完成 | `FdEventContainer`（fd→FdEvent 容器）；`read_hook`/`write_hook`（EAGAIN 时挂起协程、添加 epoll 事件、Yield）。 |

## 任务二十记录

任务二十完成的目标是建立最小协程对象，为后续 IO hook 提供基础：

- 新建 `mytinyrpc/coroutine/` 目录，包含：
  - `coctx.h`：x86-64 寄存器上下文结构体与偏移常量。
  - `coctx_swap.S`：汇编上下文切换（保存/恢复 14 个通用寄存器，复制自 Tencent/libco）。
  - `coroutine.h` / `coroutine.cc`：`Coroutine` 类，支持 `resume()` / `Yield()` / 状态管理。
- `Coroutine` 构造时 `malloc` 分配独立栈，初始化寄存器上下文，`CoFunc` 作为入口包装函数。
- `Coroutine::Yield()` 只允许在非主协程中调用。
- `Coroutine::resume()` 只允许从主协程恢复未结束协程；`Finished` 状态立即返回。
- `FdEvent` 新增 `setCoroutine()` / `getCoroutine()` / `clearCoroutine()`，只保存非拥有的 `Coroutine*`。
- 当前 `TcpConnection`、`TcpServer`、`Reactor` 的 echo 行为保持不变。

## 任务二十一记录

任务二十一完成的目标是引入 `read_hook`/`write_hook` 的最小雏形，实现"IO 暂不可用 → 协程主动让出"的挂起半边：

- 新增 `mytinyrpc/coroutine/coroutine_hook.h` / `.cc`，接口为显式传入 `FdEvent*`：
  - `read_hook(FdEvent* fdEvent, void* buf, size_t count)`：fd 从 `fdEvent->getFd()` 取。
    非主协程中遇到 `EAGAIN/EWOULDBLOCK` 时，挂载协程到传入的 `fdEvent`、添加 `EPOLLIN`、调用 `Coroutine::Yield()`；恢复后再次 `::read`。
  - `write_hook(FdEvent* fdEvent, const void* buf, size_t count)`：对称实现，`EAGAIN` 时添加 `EPOLLOUT` 并 `Yield`。
  - 主协程中直接透传系统调用结果，不做任何挂起。
- 调用方负责提供与 fd 关联的 `FdEvent`（如 `TcpConnection::m_fdEvent`），避免同一 fd 存在多个 `FdEvent` 实例。
- 不引入 `FdEventContainer`：当前 `TcpConnection` 已持有 `m_fdEvent`，不需要全局 fd→FdEvent 映射。
- 新增 `testcases/test_hook.cc`（GoogleTest）：三个用例验证 EAGAIN 时挂起、resume 后读数据、主协程直通写。
- `Reactor`、`TcpConnection`、Echo Server 的 callback 行为保持不变。

## 下一任务

下一步是任务二十二：

```text
让 Reactor 在 fd 事件到来时恢复挂在 FdEvent 上的协程，把"挂起半边"接成完整闭环
```

### 任务二十二建议要点

- 在 `Reactor::waitOnce()` 的事件分发循环中，检查触发事件对应 `FdEvent` 是否挂有协程。
- 若 `FdEvent::getCoroutine() != nullptr`，调用 `coroutine->resume()` 恢复该协程。
- 恢复后清除 `FdEvent` 上的协程指针（`clearCoroutine()`）。
- 可在 `test_hook.cc` 中增加 Reactor 驱动的端到端用例，验证完整 IO 协程化链路。
- 不涉及 `TcpConnection` 改造、协程池、Timer、IOThread。
