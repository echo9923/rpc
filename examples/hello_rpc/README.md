# hello_rpc —— mytinyrpc 框架使用示例

本目录是一个**可直接照抄**的端到端示例，演示如何用 `mytinyrpc` 框架实现两种协议：

- **TinyPB（TinyRPC）协议**：基于 Protobuf 序列化的 RPC，含服务端、同步客户端、异步客户端。
- **HTTP 协议**：基于 `HttpServlet` 的路由分发，含多个 Servlet。

## 一个关键约束

框架的 `Runtime` 在**一个进程内只支持一种协议**（由配置里的 `<protocol>` 字段决定，取值 `tinypb` 或 `http`）。
因此本示例拆成两个独立的服务端可执行文件：`hello_tinypb_server` 与 `hello_http_server`。

## 目录结构

```text
examples/hello_rpc/
├── README.md                 # 本文档
├── run.sh                    # 一键编译 + 起服务 + 验证
├── proto/
│   └── hello_rpc.proto       # TinyPB 用的 Protobuf 定义（必须开启 cc_generic_services）
├── tinypb_server.cc          # TinyPB 服务端（XML 配置驱动）
├── tinypb_client.cc          # TinyPB 客户端（同步 + 异步）
├── http_server.cc            # HTTP 服务端（手工装配 + 多 Servlet）
└── conf/
    └── tinypb_server.xml     # TinyPB 服务端配置（协议、端口、日志、IO 线程等）
```

## 构建

在仓库根目录执行：

```bash
./build.sh
```

构建产物位于 `build/` 下：`hello_tinypb_server`、`hello_tinypb_client`、`hello_http_server`。

## 一键运行

```bash
bash examples/hello_rpc/run.sh
```

脚本会自动：起 TinyPB 服务端 → 跑客户端（同步+异步）→ 起 HTTP 服务端 → 用 `curl` 验证各路由 → 清理进程。
全部通过时末尾输出 `[hello_rpc] PASS`。跳过重新编译可设环境变量 `MYTINYRPC_SKIP_BUILD=1`。

## 手动运行

### TinyPB：服务端 + 客户端

```bash
# 终端 1：启动 TinyPB 服务端（阻塞）
./build/hello_tinypb_server examples/hello_rpc/conf/tinypb_server.xml

# 终端 2：运行客户端（默认同步+异步都跑）
./build/hello_tinypb_client 23456

# 也可以单独跑某一种
./build/hello_tinypb_client --sync 23456
./build/hello_tinypb_client --async 23456
```

### HTTP：服务端 + curl

```bash
# 终端 1：启动 HTTP 服务端（阻塞）
./build/hello_http_server --server 23457

# 终端 2：curl 各路由
curl -i "http://127.0.0.1:23457/hello?name=rpc"        # 200, hello rpc
curl -i "http://127.0.0.1:23457/api/json?name=rpc"     # 200, JSON
curl -i -d 'ping' "http://127.0.0.1:23457/echo"        # 200, 回显 ping
curl -i "http://127.0.0.1:23457/error"                 # 500
```

> 框架本身没有 HTTP 客户端封装，因此 HTTP 端用 `curl` 验证。

## 代码导读

### TinyPB 协议

- **`proto/hello_rpc.proto`**：`option cc_generic_services = true;` 让 protoc 生成 `HelloService` 抽象基类与 `HelloService_Stub` 客户端存根。
- **`tinypb_server.cc`**：展示**配置驱动的启动门面**：
  ```text
  InitConfig(配置) -> StartRpcServer() -> REGISTER_SERVICE(HelloServiceImpl) -> GetServer()->start()
  ```
- **`tinypb_client.cc`**：展示两种调用方式
  - 同步：`TinyPbRpcChannel` + `Stub`，`stub.hello()` 返回即拿到结果，用 `controller.Failed()` 判错。
  - 异步：`TinyPbRpcAsyncChannel` + `Stub` + `Closure`，调用立即返回，响应到达后在回调里处理（内部由 `reqId` pending 表匹配）。

### HTTP 协议

- **`http_server.cc`**：展示**手工装配**（区别于 TinyPB 的配置驱动）：
  ```text
  HttpCodec + HttpDispatcher -> registerServlet(路径, Servlet) -> TcpServer(addr, codec, dispatcher) -> init() -> start()
  ```
  每个 `HttpServlet` 子类只需重写 `handle(request, response)`：填充响应并 `return true`；`return false` 或抛异常时 dispatcher 自动回 500。

## 框架 API 速查

| 能力 | 关键头文件 / API |
|------|-----------------|
| 启动门面 | `comm/start.h`：`InitConfig` / `StartRpcServer` / `GetServer` / `REGISTER_SERVICE` / `REGISTER_HTTP_SERVLET` |
| TinyPB 同步客户端 | `net/tinypb/tinypbrpcchannel.h`：`TinyPbRpcChannel(IPAddress)` |
| TinyPB 异步客户端 | `net/tinypb/tinypbrpcasyncchannel.h`：`TinyPbRpcAsyncChannel(IPAddress)` |
| RPC 控制器 | `net/tinypb/tinypbrpccontroller.h`：`TinyPbRpcController`（`Failed` / `ErrorText` / `setTimeout`） |
| HTTP Servlet | `net/http/httpservlet.h`：`HttpServlet::handle` |
| HTTP 请求/响应 | `net/http/httprequest.h`、`net/http/httpresponse.h`、`net/http/httpdefine.h` |
| TCP 服务端 | `net/tcpserver.h`：`TcpServer(addr, codec, dispatcher)` / `init` / `start` / `setIOThreadNum` |

## 深入参考

- 测试套件（每种 API 的真实用法）：`testcases/test_tinypb_server_client.cc`、`testcases/test_http_server.cc`
- 同类 in-tree 子工程范例：`e2e/`
- 框架源码：`mytinyrpc/comm/start.h`、`mytinyrpc/net/tinypb/*`、`mytinyrpc/net/http/*`
- 工程代码生成器（脚手架模式）：`generator/tinyrpc_generator.py`
- 编码规范：`docs/40-工程规范/编码规范.md`
