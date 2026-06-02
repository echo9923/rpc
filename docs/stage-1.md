# 阶段 1：阻塞式 TCP Echo 服务器

## 目标

阶段 1 实现一个最小化的阻塞式 TCP Echo 服务器，验证基本的 TCP 服务器链路：

```text
socket -> bind -> listen -> accept -> read -> write -> close
```

## 已实现的组件

### 日志记录器 (Logger)

文件：

- `mytinyrpc/comm/log.h`
- `mytinyrpc/comm/log.cc`

职责：

- 打印控制台日志。
- 支持 `INFO`、`DEBUG`、`WARN` 和 `ERROR` 日志级别。

### IPAddress

文件：

- `mytinyrpc/net/netaddress.h`
- `mytinyrpc/net/netaddress.cc`

职责：

- 存储 IPv4 地址和端口。
- 为 `bind()` 提供 `sockaddr` 数据。
- 提供 `toString()` 用于日志输出。

### TcpServer

文件：

- `mytinyrpc/net/tcpserver.h`
- `mytinyrpc/net/tcpserver.cc`

职责：

- 创建监听套接字。
- 设置 `SO_REUSEADDR`。
- 绑定本地地址。
- 在配置的端口上监听。
- 使用阻塞式 `accept()` 接受客户端连接。
- 为每个接受的连接创建 `TcpConnection`。

### TcpConnection

文件：

- `mytinyrpc/net/tcpconnection.h`
- `mytinyrpc/net/tcpconnection.cc`

职责：

- 管理客户端文件描述符 (fd)。
- 使用阻塞式 `read()` 读取客户端数据。
- 将相同的数据写回客户端。
- 在客户端断开连接后关闭 fd。

### Echo 服务器入口

文件：

- `testcases/test_tcp_echo_server.cc`

职责：

- 监听 `127.0.0.1:19999`。
- 启动阶段 1 的阻塞式 echo 服务器。

## 构建

```bash
./build.sh
```

## 运行

```bash
./build/test_tcp_echo_server
```

## 测试

```bash
./scripts/check_stage1.sh
```

## 验收标准

阶段 1 通过的条件：

- 项目构建成功。
- 服务器在 `127.0.0.1:19999` 上监听。
- 客户端能够连接到服务器。
- 客户端收到与其发送内容相同的文本。
- 单个连接可以发送多条 echo 消息。
- 客户端断开连接后服务器不会崩溃。
- 前一个客户端断开后，服务器能够接受下一个客户端。
- 自动验收脚本输出 `[stage1] PASS`。

## 已知限制

- 单线程阻塞模型。
- 一个长时间连接的客户端会阻塞后续客户端。
- 没有 `epoll`。
- 没有非阻塞 IO。
- 没有 HTTP。
- 没有 TinyPB。
- 没有 RPC。
- 没有协程。

## 下一阶段

阶段 2 将引入非阻塞套接字和 `epoll`，然后开始提取 Reactor 模型。
