# 错误码说明

本文记录当前同步 RPC 链路中的错误分层。框架错误码只描述框架层失败，不承载业务失败。

## 错误分层

| 层级 | 承载位置 | 含义 | 示例 |
|---|---|---|---|
| Controller error | `TinyPbRpcController::ErrorCode()` / `ErrorText()` | 客户端框架层失败，调用方不应继续信任业务 response | connect 失败、timeout、reqId mismatch、response 反序列化失败 |
| TinyPB errCode | `TinyPbStruct::m_errCode` / `m_errInfo` | 服务端框架层失败，由 dispatcher 或服务端框架返回 | service not found、method not found、request pbData 反序列化失败 |
| Business ret_code | 业务 response 字段，如 `queryNameRes.ret_code` | 业务方法正常执行后返回的业务结果 | 查询不到用户、参数校验失败 |

## 同步 RPC 核心错误码矩阵

| 错误码 | 数值 | 层级 | 触发场景 | 测试覆盖 |
|---|---:|---|---|---|
| `ERROR_FAILED_DESERIALIZE` | 100003 | Controller / TinyPB | Protobuf payload 不能反序列化 | `TinyPbRpcChannelTest.BadResponsePayloadSetsDeserializeError`、`TinyPbDispatcherTest.DispatchRejectsBadPbData` |
| `ERROR_FAILED_SERIALIZE` | 100004 | Controller / TinyPB | Protobuf 消息序列化失败 | `TinyPbDispatcherTest.DispatchCallsServiceAndSerializesResponse` 覆盖成功路径，失败码保留给不可序列化消息场景 |
| `ERROR_SERVICE_NOT_FOUND` | 100008 | TinyPB / Controller | 服务端找不到 service，客户端收到服务端框架错误 | `TinyPbDispatcherTest.DispatchRejectsUnknownService`、`TinyPbRpcChannelTest.ServerTinyPbErrorSetsControllerError` |
| `ERROR_METHOD_NOT_FOUND` | 100009 | TinyPB | 服务端找到 service 但找不到 method | `TinyPbDispatcherTest.DispatchRejectsUnknownMethod` |
| `ERROR_PARSE_SERVICE_NAME` | 100010 | TinyPB | `serviceFullName` 无法拆成 service/method | `TinyPbDispatcherTest.DispatchRejectsBadServiceFullName` |
| `ERROR_RPC_CHANNEL_INVALID_ARGUMENT` | 100011 | Controller | `TinyPbRpcChannel::CallMethod()` 参数为空 | `TinyPbRpcChannel` 参数防御路径 |
| `ERROR_RPC_CHANNEL_NETWORK` | 100012 | Controller | Channel 层网络失败但 TcpClient 未提供更具体错误码 | Channel 网络兜底路径 |
| `ERROR_RPC_REQID_MISMATCH` | 100013 | Controller | response `reqId` 与 request `reqId` 不一致 | `TinyPbRpcChannelTest.MismatchedResponseReqIdSetsControllerError` |
| `ERROR_TCP_CONNECT_FAILED` | 100014 | TcpClient / Controller | TCP 连接失败或重试耗尽 | `TcpClientTest.ConnectToNonListeningPort`、`TcpClientTest.RetryConnectStopsAfterConfiguredAttempts`、`TinyPbRpcChannelTest.NetworkFailureSetsControllerError` |
| `ERROR_TCP_SEND_FAILED` | 100015 | TcpClient / Controller | 写入时对端 reset 或 socket 写失败 | `TcpClientTest.SendTinyPbRequestFailsWhenPeerResetsConnection` |
| `ERROR_TCP_RECV_FAILED` | 100016 | TcpClient / Controller | 读取时对端提前关闭或 socket 读失败 | `TcpClientTest.RecvTinyPbResponseFailsWhenServerClosesEarly` |
| `ERROR_TCP_TIMEOUT` | 100017 | TcpClient / Controller | connect/read/write 等待超时 | `TcpClientTest.RecvTinyPbResponseTimesOutWhenServerDoesNotReply`、`TinyPbRpcChannelTest.ControllerTimeoutIsPassedToTcpClient` |

## 当前规则

- 客户端框架层失败写入 `TinyPbRpcController`。
- 服务端框架层失败写入 TinyPB response 的 `m_errCode`，Channel 收到后转写到 controller。
- 业务方法成功被调用后，业务成功或失败只写业务 response 字段。
- 同步客户端不缓存乱序响应；`reqId` 不匹配时直接失败。
- 异步 pending map 和更完整取消语义留到异步 RPC 阶段。
