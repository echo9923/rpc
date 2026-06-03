# TinyRPC 复刻学习总结

本文总结从阻塞 Echo Server 到当前 MyTinyRPC 框架的演进路径。目标不是证明项目已经生产可用，而是把每个阶段解决的问题、形成的能力和保留的边界说清楚。

## 演进主线

1. 阻塞 TCP Echo Server
   - 先用最小 socket、bind、listen、accept、read、write 建立网络直觉。
   - 关键收获：fd 生命周期和服务端阻塞模型的局限。

2. 非阻塞 fd、epoll、FdEvent、Reactor
   - 把“一个连接阻塞一个流程”改为 Reactor 统一等待事件。
   - 关键收获：fd callback 的注册、触发、删除和线程归属必须明确。

3. TcpBuffer、TcpConnection、协议抽象
   - 输入、执行、输出三段式把 TCP 字节流和协议处理拆开。
   - 关键收获：buffer 属于连接，codec 只负责字节和协议对象转换，dispatcher 只负责业务路由。

4. TinyPB codec 和 Protobuf 服务分发
   - TinyPB envelope 承载 `reqId`、服务方法名、错误码和 Protobuf payload。
   - 关键收获：框架错误、协议错误和业务 response 不能混在一起。

5. 同步 RPC Channel
   - `TinyPbRpcChannel` 让 Protobuf Stub 不再关心 TCP 和 TinyPB 编码。
   - 关键收获：`RpcChannel` 是 Stub 和网络传输层之间的适配器。

6. 同步客户端语义
   - 补齐请求号、controller、超时、连接失败、重连和错误码矩阵。
   - 关键收获：同步单请求模型不需要 pending map，`reqId` mismatch 直接失败更清晰。

7. Timer、wakeup、task queue 和连接生命周期
   - `timerfd`、`eventfd`、task queue 和 `stop()` 让 Reactor 可被跨线程驱动。
   - 关键收获：Timer callback、fd callback 和 task 都必须落在明确的 Reactor 线程。

8. IOThreadPool 和多 Reactor TcpServer
   - Main Reactor accept，Sub Reactor 处理连接读写。
   - 关键收获：连接归属线程比“用了几个线程”更重要。

9. HTTP 协议栈
   - 在同一套 `TcpServer` / `TcpConnection` / `AbstractCodec` / `AbstractDispatcher` 上接入 HTTP。
   - 关键收获：协议复用靠抽象边界，而不是复制一套 server。

10. 配置、日志、启动入口和运行时
    - XML 配置、日志级别、启动宏和 request context 让测试程序向框架入口靠近。
    - 关键收获：运行时上下文要小而明确，避免跨请求污染。

11. 协程、hook、协程池和内存池
    - 整理 `Yield()` / `resume()`，补充 connect、sleep、socket hook。
    - 关键收获：hook 本质上是“遇到会阻塞的系统调用时把等待交给 Reactor”。

12. 异步 RPC Channel
    - pending map、IOThread、timeout、cancel 和 closure 生命周期形成异步 RPC 主干。
    - 关键收获：成功响应、网络失败、超时和取消都必须通过同一个 pending 仲裁入口，避免二次回调。

13. 生成器与示例工程
    - 从简单 proto 生成 service 实现占位、client 调用、CMake 工程和 run/shutdown 脚本。
    - 关键收获：生成器先服务于可理解、可运行，不急于完整解析 Protobuf。

14. 工程收口
    - 补齐目录说明、覆盖矩阵、一键全量回归、examples 和学习总结。
    - 关键收获：学习型项目也需要可回归、可定位、可解释的收口材料。

## 当前可展示能力

- TinyPB 同步 RPC：`scripts/check_rpc_sync.sh`
- TinyPB 异步 RPC：`scripts/check_rpc_async.sh`
- 多 Reactor TCP server：`scripts/check_stage11_server.sh`
- HTTP server：`scripts/check_stage12_http.sh`
- 生成工程端到端：`scripts/check_generator_project.sh`
- 全量回归：`scripts/check_all.sh`

## 与原 TinyRPC 的关系

当前项目保留 TinyRPC 的核心学习对象：

- Reactor / Timer / IOThread / IOThreadPool。
- TcpServer / TcpConnection / TcpClient。
- TinyPB codec / dispatcher / RpcChannel / RpcController。
- HTTP codec / dispatcher / servlet。
- Config / Log / Runtime / Start。
- Coroutine hook 和基础协程复用。
- Generator。

主要简化点：

- 不做 MySQL 插件。
- 不做完整连接池和负载均衡。
- 不做 HTTPS、HTTP/2、chunked 或 streaming response。
- 不做完整 tracing 系统。
- 不做完整 Protobuf parser。
- 不做性能压测报告。

更详细的覆盖状态见 [原 TinyRPC 功能覆盖矩阵](original-coverage-matrix.md)。

## 继续演进建议

1. 给 `TcpServer` 增加框架层 `stop()`，替代脚本杀进程。
2. 把异步 RPC 的网络路径从“IOThread 内同步 TcpClient”继续推进到真正非阻塞连接。
3. 为 HTTP 增加大小写无关 header、keep-alive 边界和更多错误响应。
4. 让生成器理解 package / namespace，并生成更真实的业务字段填充样例。
5. 根据需要再规划连接池、负载均衡和 tracing，而不是提前塞入主链路。
