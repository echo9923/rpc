# 阶段 10：Timer、Reactor wakeup 和连接生命周期

阶段 10 的目标是让 Reactor 从只处理 fd 事件，逐步升级到可以处理时间事件、跨线程任务投递和安全退出。本阶段会先完成内存级 TimerEvent，再接入 timerfd，最后补齐 wakeup、stop 和连接生命周期文档。

## 任务四十七：`TimerEvent` 与基础时间函数

已完成能力：

- 新增 `getNowMs()`，返回当前毫秒时间，用作定时任务到期时间基准。
- 新增 `TimerEvent`，描述一个内存级定时任务。
- 支持一次性任务：`run()` 后执行 callback 并进入 canceled 状态。
- 支持重复任务：`run()` 后执行 callback，并刷新下一次到期时间。
- 支持 `cancel()`：取消后 `run()` 不再执行 callback。
- 支持 `resetTime()`：按原 interval 或新 interval 刷新到期时间，并清除 canceled 状态。

## 当前边界

- `TimerEvent` 只描述任务本身，不创建 `timerfd`。
- `TimerEvent` 不注册到 Reactor，也不负责调度顺序。
- 到期判断由 `isExpired(nowMs)` 提供，真正的到期扫描和执行留给后续 `Timer`。
- `run()` 不主动检查当前时间是否已到期；调用方必须只在确认到期后调用。

## 验证命令

```bash
./build.sh
./build/test_timer_event
./scripts/check_rpc_sync.sh
```
