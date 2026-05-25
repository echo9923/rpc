# 阶段 2：非阻塞 IO 与 Reactor 准备

## 目标

阶段 2 从阶段 1 的阻塞式 TCP Echo Server 出发，逐步引入非阻塞套接字、`epoll` 和 Reactor 模型。

阶段 2 的推进顺序：

```text
任务八：listen fd 非阻塞
任务九：client fd 非阻塞，并处理 read/write 的 EAGAIN
任务十：引入最小 epoll 测试
任务十一：抽象 FdEvent
任务十二：抽象 Reactor
```

当前阶段仍以学习网络 IO 行为为主，不追求并发性能。

## 当前进度

| 任务 | 状态 | 说明 |
|------|------|------|
| 任务八：listen fd 非阻塞 | 已完成 | 已封装 fd 工具函数，并让监听 fd 进入非阻塞模式。 |
| 任务九：client fd 非阻塞 + EAGAIN | 未开始 | 下一步处理客户端 fd 的非阻塞读写行为。 |
| 任务十：最小 epoll | 未开始 | 暂不引入。 |
| 任务十一：FdEvent | 未开始 | 暂不抽象。 |
| 任务十二：Reactor | 未开始 | 暂不抽象。 |

## 任务八记录

任务八完成的目标是先掌握监听套接字的非阻塞行为：

- 新增 fd 工具函数，用于设置 `O_NONBLOCK`。
- 将 `SO_REUSEADDR` 的设置逻辑收敛到 fd 工具函数中。
- `TcpServer::init()` 创建监听 fd 后，设置 `SO_REUSEADDR` 和 `O_NONBLOCK`。
- `TcpServer::acceptLoop()` 遇到 `EAGAIN` 或 `EWOULDBLOCK` 时，不打印错误日志。
- 空闲无连接时，使用短暂 `usleep(1000)` 避免 busy loop。
- 暂时保持 client fd 为阻塞模式，避免提前扰动 echo 行为。
- 暂时不引入 `epoll`、`FdEvent` 或 Reactor。

任务八完成后，服务端形态是：

```text
非阻塞 listen fd + 阻塞 client fd + 临时 accept/read/write 循环
```

## 下一任务

下一步是任务九：

```text
client fd 非阻塞 + read/write 的 EAGAIN 处理
```

任务九建议完成：

- `accept()` 成功后，对 client fd 调用 `setNonBlock()`。
- `TcpConnection::readData()` 识别 `EAGAIN` 和 `EWOULDBLOCK`。
- `TcpConnection::writeData()` 识别 `EAGAIN` 和 `EWOULDBLOCK`。
- `writeData()` 开始处理部分写。
- 仍然不引入 `epoll`。
- 第一阶段验收脚本仍然输出 `[stage1] PASS`。

## 验收命令

每完成阶段 2 的一个小任务，都需要至少执行：

```bash
./build.sh
```

```bash
./scripts/check_stage1.sh
```

期望结果：

```text
build success
[stage1] PASS
```

## 待确认项

- 确认 fd 工具文件名与 include、CMake 源文件列表保持一致。
- 重新执行 `./build.sh` 和 `./scripts/check_stage1.sh`，确认任务八没有破坏阶段 1 验收。
