# 阶段 11：IOThread 与服务端多 Reactor

阶段 11 的目标是把当前单 Reactor 模型推进到 Main Reactor accept、Sub Reactor 处理连接读写的服务端模型。本阶段会先补基础线程工具，再实现 IOThread、IOThreadPool，最后让 TcpServer 可以把新连接分发到 Sub Reactor。

## 任务五十三：`Mutex`、`RWMutex` 和基础线程工具

已完成能力：

- 新增 `Mutex`，封装 `std::mutex`。
- 新增 `MutexLockGuard`，用 RAII 方式进入和退出互斥区。
- 新增 `RWMutex`，封装 `std::shared_mutex`。
- 新增 `ReadLockGuard` 和 `WriteLockGuard`，分别封装共享读锁和独占写锁。
- 新增 `test_mutex`，覆盖多线程递增、多读并发进入、写锁独占递增。

## 当前边界

- 当前锁工具只做最小封装，不复刻原项目复杂锁实现。
- 不做无锁结构。
- 暂未替换 Reactor 内部已有 `std::mutex`，后续 IOThreadPool 需要统一风格时再逐步调整。

## 验证命令

```bash
./build.sh
./build/test_mutex
./scripts/check_rpc_sync.sh
```
