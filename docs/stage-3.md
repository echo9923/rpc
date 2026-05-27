# 阶段 3：协程与 IO Hook

## 目标

阶段 3 在阶段 2（非阻塞 IO + Reactor）的基础上引入协程，将网络 IO 从回调驱动转变为协程驱动，使业务代码可以按同步风格编写。

## 当前进度

| 任务 | 状态 | 说明 |
|------|------|------|
| 任务二十：抽象最小协程对象与 FdEvent 挂载点 | 已完成 | 最小 `Coroutine` 类（创建/恢复/让出/完成）；`FdEvent` 挂载 `Coroutine*`。 |

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

## 下一任务

下一步是任务二十一：

```text
引入 read_hook/write_hook 的最小雏形
```

### 任务二十一建议要点

- 在封装 socket read/write 的工具函数中检测 `EAGAIN`。
- 遇到 `EAGAIN` 时，将当前协程挂到 `FdEvent` 上，然后 `Yield()` 让出执行权。
- 当前 `Reactor::waitOnce()` 在 `handleEvent()` 回调中，不直接恢复协程。
- 继续保持 callback 驱动；`read_hook/write_hook` 只负责挂起当前协程。
- 可以新增 `test_hook.cc` 或扩展 `test_coroutine.cc` 验证挂起行为。
- 不涉及 `TcpConnection` 改造、协议编解码、半包/粘包处理。
