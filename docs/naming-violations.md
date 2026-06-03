# 命名规范整改记录

生成日期：2026-06-03

依据：[编码规范](40-工程规范/编码规范.md)：

- 类型名：类名、结构体名使用 PascalCase。
- 成员变量：`m_` 前缀 + lowerCamelCase。
- 成员方法：普通方法使用 lowerCamelCase，获取类方法使用 `get` 前缀，判断类方法使用 `is`/`has` 前缀。
- 普通函数：使用 lowerCamelCase。
- 文件名：全英文小写；同一模块的头文件与实现文件命名保持一致。
- 测试入口文件：放在 `testcases/` 目录，文件名保持英文小写；新增测试优先使用 `test_*.cc`。

## 本次整改结论

本轮在原先“三类命名不规范”整改基础上，继续补齐用户指出的文件名和普通函数命名问题，包含 `MsgReq -> ReqId`、`read_hook -> readHook`、源码文件名去下划线、协程汇编文件名与符号同步、CMake/脚本/生成器模板引用同步。

| 类别 | 整改后状态 | 说明 |
|------|------------|------|
| 类型名不符合 PascalCase | 0 | `coctx` 已改为 `Coctx` |
| 项目自有成员变量不符合规范 | 0 | 已统一为 `m_` 前缀 + lowerCamelCase |
| 项目自有成员方法不符合规范 | 0 | 已统一为 lowerCamelCase / `get` / `is` |
| 项目自有普通函数不符合 lowerCamelCase | 0 | `read_hook` 等 hook 函数已改为 lowerCamelCase |
| 项目源码文件名不符合全小写英文规范 | 0 | 源码文件名已去掉下划线并同步 include/CMake |
| `MsgReq` 语义命名不准确 | 0 | 已统一改为 `ReqId` / `reqId` / `req_id` |

说明：

- Protobuf、GoogleTest 等外部框架强制接口名未改，例如 `CallMethod()`、`Reset()`、`SetFailed()`、`StartCancel()`、`IsCanceled()`、`NotifyOnCancel()`、`SetUp()`、`TearDown()`、`Run()`。
- `testcases/` 下的测试文件遵守 `test_*.cc` 入口命名规范，因此不会把所有测试文件里的下划线都去掉。例如 `test_hook_socket.cc`、`test_iothread_pool.cc`、`test_http_define.cc` 保留不变是符合规范的。
- 本轮只将方案指定的 `test_msg_req.cc` 改为 `test_req_id.cc`。
- `AGENTS.md`、`CMakeLists.txt`、`README.md` 等工具约定文件不是项目源码模块文件，未按“全小写无下划线模块文件名”处理。

## ReqId 命名整改

| # | 原名称 | 新名称 | 说明 |
|---|--------|--------|------|
| 1 | `MsgReqUtil` | `ReqIdUtil` | 请求号工具类 |
| 2 | `genMsgNumber()` | `genReqId()` | 生成请求号 |
| 3 | `genMsgReq()` | `genReqId()` | Channel 内部请求号生成方法 |
| 4 | `setMsgReqGenerator()` | `setReqIdGenerator()` | 注入请求号生成器 |
| 5 | `m_msgReqGenerator` | `m_reqIdGenerator` | 请求号生成器成员 |
| 6 | `setMsgReq()` | `setReqId()` | Controller 请求号写入接口 |
| 7 | `getMsgReq()` | `getReqId()` | Controller 请求号读取接口 |
| 8 | `m_msgReq` | `m_reqId` | 请求号成员 |
| 9 | `m_msgReqLen` | `m_reqIdLen` | TinyPB 请求号长度字段 |
| 10 | `ERROR_RPC_MSGREQ_MISMATCH` | `ERROR_RPC_REQID_MISMATCH` | 请求号不匹配错误码 |
| 11 | `comm/msgreq.h` | `comm/reqid.h` | 工具头文件 |
| 12 | `comm/msgreq.cc` | `comm/reqid.cc` | 工具实现文件 |
| 13 | `testcases/test_msg_req.cc` | `testcases/test_req_id.cc` | 请求号测试文件 |

## 普通函数命名整改

| # | 原函数 | 新函数 |
|---|--------|--------|
| 1 | `read_hook()` | `readHook()` |
| 2 | `write_hook()` | `writeHook()` |
| 3 | `recv_hook()` | `recvHook()` |
| 4 | `send_hook()` | `sendHook()` |
| 5 | `accept_hook()` | `acceptHook()` |
| 6 | `connect_hook()` | `connectHook()` |
| 7 | `sleep_hook()` | `sleepHook()` |
| 8 | `usleep_hook()` | `usleepHook()` |

## 源码文件名整改

| # | 原文件 | 新文件 |
|---|--------|--------|
| 1 | `mytinyrpc/comm/msgreq.h` | `mytinyrpc/comm/reqid.h` |
| 2 | `mytinyrpc/comm/msgreq.cc` | `mytinyrpc/comm/reqid.cc` |
| 3 | `mytinyrpc/coroutine/coctx_swap.S` | `mytinyrpc/coroutine/coctxswap.s` |
| 4 | `mytinyrpc/coroutine/coroutine_hook.h` | `mytinyrpc/coroutine/coroutinehook.h` |
| 5 | `mytinyrpc/coroutine/coroutine_hook.cc` | `mytinyrpc/coroutine/coroutinehook.cc` |
| 6 | `mytinyrpc/coroutine/coroutine_pool.h` | `mytinyrpc/coroutine/coroutinepool.h` |
| 7 | `mytinyrpc/coroutine/coroutine_pool.cc` | `mytinyrpc/coroutine/coroutinepool.cc` |
| 8 | `mytinyrpc/net/iothread_pool.h` | `mytinyrpc/net/iothreadpool.h` |
| 9 | `mytinyrpc/net/iothread_pool.cc` | `mytinyrpc/net/iothreadpool.cc` |
| 10 | `mytinyrpc/net/tcpconnection_timewheel.h` | `mytinyrpc/net/tcpconnectiontimewheel.h` |
| 11 | `mytinyrpc/net/tcpconnection_timewheel.cc` | `mytinyrpc/net/tcpconnectiontimewheel.cc` |
| 12 | `mytinyrpc/net/http/http_define.h` | `mytinyrpc/net/http/httpdefine.h` |
| 13 | `mytinyrpc/net/http/http_define.cc` | `mytinyrpc/net/http/httpdefine.cc` |
| 14 | `mytinyrpc/net/http/http_request.h` | `mytinyrpc/net/http/httprequest.h` |
| 15 | `mytinyrpc/net/http/http_request.cc` | `mytinyrpc/net/http/httprequest.cc` |
| 16 | `mytinyrpc/net/http/http_response.h` | `mytinyrpc/net/http/httpresponse.h` |
| 17 | `mytinyrpc/net/http/http_response.cc` | `mytinyrpc/net/http/httpresponse.cc` |

## 类型名整改

| # | 位置 | 原名称 | 新名称 |
|---|------|--------|--------|
| 1 | `mytinyrpc/coroutine/coctx.h` | `coctx` | `Coctx` |

## 项目自有成员变量整改

| # | 类 / 结构体 | 原成员 | 新成员 |
|---|-------------|--------|--------|
| 1 | `Coctx` | `regs` | `m_regs` |
| 2 | `Coroutine` | `t_mainCoroutine` | `m_mainCoroutine` |
| 3 | `Coroutine` | `t_curCoroutine` | `m_currentCoroutine` |
| 4 | `ConnectHookState` | `coroutine` | `m_coroutine` |
| 5 | `ConnectHookState` | `fdEvent` | `m_fdEvent` |
| 6 | `ConnectHookState` | `timedOut` | `m_timedOut` |
| 7 | `ConnectHookState` | `finished` | `m_finished` |
| 8 | `SleepHookState` | `coroutine` | `m_coroutine` |
| 9 | `SleepHookState` | `finished` | `m_finished` |
| 10 | `FdHookWaitState` | `coroutine` | `m_coroutine` |
| 11 | `FdHookWaitState` | `fdEvent` | `m_fdEvent` |
| 12 | `FdHookWaitState` | `timedOut` | `m_timedOut` |
| 13 | `FdHookWaitState` | `finished` | `m_finished` |
| 14 | `TcpConnectionTimeWheel::Entry` | `connection` | `m_connection` |
| 15 | `TcpConnectionTimeWheel::Entry` | `timerTask` | `m_timerTask` |
| 16 | `AsyncCallContext` | `msgReq` | `m_reqId` |
| 17 | `AsyncCallContext` | `methodFullName` | `m_methodFullName` |
| 18 | `AsyncCallContext` | `tinyRequest` | `m_tinyRequest` |
| 19 | `AsyncCallContext` | `controller` | `m_controller` |
| 20 | `AsyncCallContext` | `request` | `m_request` |
| 21 | `AsyncCallContext` | `response` | `m_response` |
| 22 | `AsyncCallContext` | `done` | `m_done` |
| 23 | `AsyncCallContext` | `timeoutTask` | `m_timeoutTask` |
| 24 | `StringData` | `payload` | `m_payload` |
| 25 | `ContextServiceImpl` | `observedMsgReq` | `m_observedReqId` |
| 26 | `ContextServiceImpl` | `observedMethod` | `m_observedMethod` |
| 27 | `ContextServiceImpl` | `observedLocalAddr` | `m_observedLocalAddr` |
| 28 | `ContextServiceImpl` | `observedPeerAddr` | `m_observedPeerAddr` |
| 29 | `SocketPairConnection` | `reactor` | `m_reactor` |
| 30 | `SocketPairConnection` | `peerFd` | `m_peerFd` |
| 31 | `SocketPairConnection` | `connection` | `m_connection` |

## 项目自有成员方法整改

| # | 类 | 原方法 | 新方法 |
|---|----|--------|--------|
| 1 | `Coroutine` | `Yield()` | `yield()` |
| 2 | `Coroutine` | `GetCurrentCoroutine()` | `getCurrentCoroutine()` |
| 3 | `Coroutine` | `GetMainCoroutine()` | `getMainCoroutine()` |
| 4 | `Coroutine` | `IsMainCoroutine()` | `isMainCoroutine()` |
| 5 | `Coroutine` | `CoFunc()` | `coFunc()` |
| 6 | `IOThreadPool` | `size()` | `getSize()` |
| 7 | `TinyPbRpcController` | `SetError()` | `setError()` |
| 8 | `TinyPbRpcController` | `ErrorCode()` | `getErrorCode()` |
| 9 | `TinyPbRpcController` | `SetMsgReq()` | `setReqId()` |
| 10 | `TinyPbRpcController` | `MsgReq()` | `getReqId()` |
| 11 | `TinyPbRpcController` | `SetTimeout()` | `setTimeout()` |
| 12 | `TinyPbRpcController` | `Timeout()` | `getTimeout()` |
| 13 | `TinyPbRpcController` | `SetCancelCallback()` | `setCancelCallback()` |
| 14 | `TinyPbRpcController` | `ClearCancelCallback()` | `clearCancelCallback()` |

## 汇编关联整改

| # | 位置 | 原名称 | 新名称 | 说明 |
|---|------|--------|--------|------|
| 1 | `mytinyrpc/coroutine/coctx_swap.S` | `coctx_swap` | `coctxSwap` | 汇编导出符号改为 lowerCamelCase |
| 2 | `mytinyrpc/coroutine/coctx.h` | `coctx_swap(...)` | `coctxSwap(...)` | C 接口声明同步改名 |
| 3 | `mytinyrpc/coroutine/coroutine.cc` | `coctx_swap(...)` | `coctxSwap(...)` | 协程上下文切换调用点同步改名 |
| 4 | `mytinyrpc/coroutine/coctx_swap.S` | 文件名 | `mytinyrpc/coroutine/coctxswap.s` | CMake、生成器模板和生成项目同步使用新汇编文件 |

## 验证结果

已在 WSL Linux 环境中执行：

```bash
wsl --cd "D:\codeproject\cpp\rpc" bash -lc "rm -rf build && bash build.sh"
wsl --cd "D:\codeproject\cpp\rpc" bash -lc "rm -rf build && bash scripts/check_all.sh"
```

验证结果：

- `build.sh` 干净构建通过。
- `scripts/check_all.sh` 完整回归通过，最终输出 `[all] PASS`。
- 生成器回归中的生成项目重新编译了 `mytinyrpc/coroutine/coctxswap.s`，并通过服务端/客户端运行检查。
- 针对旧命名执行 `rg` 复查后，源码、CMake、脚本、生成器模板里未发现 `read_hook`、`write_hook`、`MsgReq`、`msgReq`、`msg_req`、`coctx_swap` 等旧符号残留；记录文档中的旧名只作为“原名 -> 新名”对照保留。
