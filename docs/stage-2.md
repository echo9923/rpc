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
任务十三：Reactor accept 测试
任务十四：TcpServer 接入 Reactor
任务十五：连接关闭回调
任务十六：Reactor 读写事件
任务十七：TcpConnection 引入输出缓冲区
任务十八：TcpConnection 引入输入缓冲区
任务十九：对齐 FdEvent 与 Reactor 职责边界
```

当前阶段仍以学习网络 IO 行为为主，不追求并发性能。任务十八完成后，`TcpConnection` 的基本形态是：输入缓冲区 + 输出缓冲区 + Reactor 读写事件。任务十九会暂缓协议层，先把 `FdEvent` 与 `Reactor` 的职责边界调整到更接近原 TinyRPC，为后续协程 hook 做准备。

## 当前进度

| 任务 | 状态 | 说明 |
|------|------|------|
| 任务八：listen fd 非阻塞 | 已完成 | 已封装 fd 工具函数，并让监听 fd 进入非阻塞模式。 |
| 任务九：client fd 非阻塞 + EAGAIN | 已完成 | client fd 已进入非阻塞模式，并处理 read/write 的 EAGAIN。 |
| 任务十：最小 epoll | 已完成 | 已新增最小 epoll 行为测试。 |
| 任务十一：FdEvent | 已完成 | 已抽象 fd、关注事件和回调。 |
| 任务十二：Reactor | 已完成 | 已抽象 epoll 事件循环。 |
| 任务十三：Reactor accept 测试 | 已完成 | 已验证 Reactor 驱动 accept 事件。 |
| 任务十四：TcpServer 接入 Reactor | 已完成 | Echo Server 已由 Reactor 驱动连接和读事件。 |
| 任务十五：连接关闭回调 | 已完成 | 客户端关闭后，服务端能删除连接并关闭 fd。 |
| 任务十六：Reactor 读写事件 | 已完成 | 已支持 EPOLLIN/EPOLLOUT 读写事件切换。 |
| 任务十七：TcpConnection 引入输出缓冲区 | 已完成 | 写路径已使用 `TcpBuffer` 缓存待发送数据并处理短写。 |
| 任务十八：TcpConnection 引入输入缓冲区 | 已完成 | 读路径已先写入输入 `TcpBuffer`，再取出全部可读数据交给 Echo 逻辑。 |
| 任务十九：对齐 FdEvent 与 Reactor 职责边界 | 已完成 | `FdEvent` 已保存所属 `Reactor` 并负责注册、更新和注销事件。 |

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

## 任务十八记录

任务十八完成的目标是让 socket 读到的数据先进入输入缓冲区：

- `TcpConnection` 新增 `TcpBuffer m_inputBuffer`。
- `TcpConnection::readData()` 继续负责非阻塞 `read()`。
- `read()` 成功读到数据后，先调用 `m_inputBuffer.append()` 追加到输入缓冲区。
- 当前仍保持 Echo 行为，暂时通过 `m_inputBuffer.retrieveAllAsString()` 取出全部可读数据，再调用 `sendData()`。
- `TcpBuffer` 保持原接口，继续提供追加、取出和自动扩容能力。
- 新增分段追加测试，覆盖输入缓冲区累计多次读入后一次性取出的用法。

任务十八完成后，服务端形态是：

```text
非阻塞 listen fd + 非阻塞 client fd + Reactor + FdEvent + TcpConnection 输入/输出缓冲区
```

## 任务十九记录

任务十九完成的目标是把连接 fd 的事件生命周期收回到 `FdEvent`：

- `FdEvent` 新增所属 `Reactor*` 和注册状态。
- `FdEvent` 新增 `setReactor()`、`getReactor()`、`registerToReactor()`、`updateToReactor()`、`unregisterFromReactor()` 和 `isRegistered()`。
- `TcpConnection` 仍保存所属 `Reactor`，但只用于初始化自己的 `FdEvent`。
- `TcpConnection` 不再保存重复注册状态，fd 是否已注册统一由 `FdEvent` 判断。
- `TcpConnection` 注册、更新和注销连接事件时，统一调用 `FdEvent` 的接口。
- `TcpConnection` 不再直接调用 `Reactor::addEvent()`、`Reactor::modEvent()` 或 `Reactor::delEvent()`。
- `Reactor` 继续保留底层 epoll 注册、修改和删除能力。
- 当前 Echo 行为保持不变，协议编解码和协程 hook 继续后置。

任务十九完成后，服务端形态是：

```text
非阻塞 listen fd + 非阻塞 client fd + Reactor + 可自注册 FdEvent + TcpConnection 输入/输出缓冲区
```

## 下一任务

下一步是任务二十：

```text
抽象最小协程对象
```

任务二十建议完成：

- 先引入不接管网络 IO 的最小协程对象。
- 只验证协程创建、恢复、让出和结束状态。
- 不在任务二十中引入 hook、Timer、IOThread 或协议编解码。

## 验收命令

每完成阶段 2 的一个小任务，都需要至少执行：

```bash
./build.sh
```

```bash
./scripts/check_stage1.sh
```

```bash
./build/test_tcp_buffer
```

```bash
./build/test_reactor
```

```bash
./build/test_fdevent
```

期望结果：

```text
build success
[stage1] PASS
[tcp_buffer] PASS
[reactor] PASS
[fdevent] PASS
```

## 待确认项

- 手动 Echo 验证连续输入 `hello`、`world`、`tinyrpc` 都能收到响应。
- 手动发送约 3000 字节数据，确认能够完整 echo 回来。
- 同时打开两个 `nc` 连接，分别发送数据，确认两个连接能收到各自响应。
- 客户端主动断开后，确认服务端能删除连接并关闭 fd，不崩溃。
