# 阶段 6：RPC 服务注册与分发

## 目标

阶段 6 在阶段 5（连接层协议接入）的基础上，引入 Protobuf 服务描述信息，实现服务注册、方法查找和错误响应。最终目标是完成 `pbData → request message → CallMethod → response message → pbData` 的完整 RPC 调用链路。

## 当前进度

| 任务 | 状态 | 说明 |
|------|------|------|
| 任务三十四：接入 Protobuf 示例服务，并让 TinyPbDispatcher 支持服务注册和方法查找 | 已完成 | dispatcher 能注册/查找 Protobuf Service，未知服务/方法返回错误响应。 |
| 任务三十五：打通服务端 RPC 内部闭环，从 TinyPB 请求帧到 CallMethod 再到 TinyPB 响应帧 | 已完成 | dispatch() 真正反序列化 pbData、调用 CallMethod、序列化响应。 |

## 任务三十四记录

任务三十四完成的目标是将 TinyPbDispatcher 从"假响应器"推进到"认识 Protobuf 服务和方法的 dispatcher"。

- 新增 `testcases/test_tinypb_server.proto`：
  - 定义 `queryNameReq`、`queryNameRes` 消息和 `QueryService` 服务。
  - `cc_generic_services = true` 使 protoc 生成 `QueryService` 基类。
- 新增 `mytinyrpc/comm/errorcode.h`：
  - 定义错误码常量：`ERROR_PARSE_SERVICE_NAME`、`ERROR_SERVICE_NOT_FOUND`、`ERROR_METHOD_NOT_FOUND`、`ERROR_FAILED_DESERIALIZE`、`ERROR_FAILED_SERIALIZE`。
- 修改 `mytinyrpc/net/tinypb/tinypbdispatcher.h`：
  - 新增 `ServicePtr` 类型别名（`std::shared_ptr<google::protobuf::Service>`）。
  - 新增 `m_serviceMap` 成员（`std::map<std::string, ServicePtr>`）。
  - 新增 `registerService()`、`findService()`、`findMethod()` 三个公共方法。
- 修改 `mytinyrpc/net/tinypb/tinypbdispatcher.cc`：
  - `registerService()`：以 `service->GetDescriptor()->full_name()` 为 key 存入注册表，重复注册返回 false。
  - `findService()`：在 `m_serviceMap` 中按服务名查找，找不到返回 nullptr。
  - `findMethod()`：通过 `service->GetDescriptor()->FindMethodByName()` 查找方法。
  - `dispatch()` 重写为四步流程：
    1. 解析 `serviceFullName` → 失败返回 `ERROR_PARSE_SERVICE_NAME`。
    2. 查找 Service → 找不到返回 `ERROR_SERVICE_NOT_FOUND`。
    3. 查找 Method → 找不到返回 `ERROR_METHOD_NOT_FOUND`。
    4. 全部找到 → 构造最小成功响应（暂不调用 CallMethod）。
- 修改 `mytinyrpc/net/tcpserver.h` + `tcpserver.cc`：
  - 新增 `registerService()` 公共方法，内部 `dynamic_cast` 到 `TinyPbDispatcher` 后转发。
- 修改 `CMakeLists.txt`：
  - `find_package(Protobuf REQUIRED)` + `protobuf_generate_cpp()`。
  - 所有可执行目标链接 `${Protobuf_LIBRARIES}`。
  - 新增 `test_protobuf_service` 编译目标。
- 新增 `testcases/test_protobuf_service.cc`：
  - `ServiceDescriptorName`：验证 `QueryService` descriptor 的 `full_name`。
  - `FindMethodByName`：验证能找到 `query_name` 方法。
  - `SerializeRequest`：验证 `queryNameReq` 能序列化和反序列化。
  - `ResponseFieldRoundTrip`：验证 `queryNameRes` 能设置字段并读回。
- 扩展 `testcases/test_tinypb_dispatcher.cc`：
  - 新增 `QueryServiceImpl` 测试辅助类。
  - `RegisterServiceStoresByFullName`：注册后能按 full_name 找到，重复注册返回 false。
  - `FindMethodReturnsDescriptor`：注册后能找到 `query_name` 方法。
  - `DispatchWritesTinyPbResponse`：注册服务后 dispatch 成功路径，响应字段正确。
  - `DispatchRejectsUnknownService`：未知服务返回 `ERROR_SERVICE_NOT_FOUND`。
  - `DispatchRejectsUnknownMethod`：未知方法返回 `ERROR_METHOD_NOT_FOUND`。
  - `DispatchRejectsBadServiceFullName`：非法 serviceFullName 返回 `ERROR_PARSE_SERVICE_NAME`。

## 任务三十五记录

任务三十五完成的目标是打通服务端 RPC 内部闭环：`TinyPB 请求帧 → decode → 反序列化 pbData → CallMethod → 序列化 response → encode → TinyPB 响应帧`。

- 新增 `mytinyrpc/net/tinypb/tinypbrpccontroller.h` + `tinypbrpccontroller.cc`：
  - `TinyPbRpcController` 继承 `google::protobuf::RpcController`，实现最小错误状态管理。
  - `SetFailed()` / `Failed()` / `ErrorText()` 用于传递 CallMethod 中的错误信息。
  - `StartCancel()` / `IsCanceled()` / `NotifyOnCancel()` 为空实现，后续扩展。
- 修改 `mytinyrpc/net/tinypb/tinypbdispatcher.cc`：
  - `dispatch()` 第四步改为真正的 RPC 调用链路：
    1. `GetRequestPrototype(method).New()` 创建请求消息。
    2. `ParseFromString(request->m_pbData)` 反序列化 → 失败返回 `ERROR_FAILED_DESERIALIZE`。
    3. `TinyPbRpcController controller` + `CallMethod()` 调用服务方法。
    4. 检查 `controller.Failed()` → 失败返回错误信息。
    5. `SerializeToString(&reply.m_pbData)` 序列化响应 → 失败返回 `ERROR_FAILED_SERIALIZE`。
  - 使用 `std::unique_ptr<Message>` 管理 `New()` 出来的对象。
- 修改 `mytinyrpc/net/tcpconnection.h`：
  - 新增 `getInputBuffer()` 公共方法。
  - `execute()` 从 private 移到 public，供外部驱动连接处理。
- 修改 `CMakeLists.txt`：SRC 列表新增 `tinypbrpccontroller.cc`。
- 扩展 `testcases/test_tinypb_dispatcher.cc`：
  - `QueryServiceImpl::query_name()` 改为真实实现：填充 ret_code、res_info、req_no、id、name。
  - `DispatchCallsServiceAndSerializesResponse`：构造真实 Protobuf 请求，验证响应 pbData 能反序列化为 queryNameRes，name == "Alice"。
  - `DispatchRejectsBadPbData`：非法 pbData 返回 `ERROR_FAILED_DESERIALIZE`。
- 扩展 `testcases/test_connection_codec.cc`：
  - `ExecuteDispatchesTinyPbRpcRequest`：从 `execute()` 入口打通整条链路，验证 CallMethod 真正被调用，响应 pbData 可反序列化为 queryNameRes。
