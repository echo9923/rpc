#include "net/tinypb/tinypbdispatcher.h"

#include "comm/errorcode.h"
#include "comm/log.h"
#include "comm/runtime.h"
#include "net/tcpconnection.h"
#include "net/tinypb/tinypbdata.h"
#include "net/tinypb/tinypbrpccontroller.h"

#include <google/protobuf/message.h>

#include <memory>
#include <string>

namespace tinyrpc {

namespace {

class RequestContextGuard {
 public:
    RequestContextGuard(
        const std::string& reqId,
        const std::string& methodName,
        const std::string& localAddr,
        const std::string& peerAddr
    )
    {
        getRuntime().setCurrentRequestContext(reqId, methodName, localAddr, peerAddr);
    }

    ~RequestContextGuard()
    {
        getRuntime().clearCurrentRequestContext();
    }
};

}  // namespace

bool TinyPbDispatcher::registerService(ServicePtr service)
{
    if (service == nullptr) {
        return false;
    }

    // [第三方 API] GetDescriptor() 返回 ServiceDescriptor，full_name() 是 Protobuf 服务的全限定名，
    // 例如 "QueryService"。以此作为注册表的 key。
    std::string name = service->GetDescriptor()->full_name();
    if (name.empty()) {
        ErrorLog("registerService: service full_name is empty");
        return false;
    }

    // 重复注册同一服务名时不覆盖，返回 false。
    if (m_serviceMap.find(name) != m_serviceMap.end()) {
        ErrorLog("registerService: service already registered, name = " + name);
        return false;
    }

    m_serviceMap[name] = std::move(service);
    InfoLog("registerService: registered service " + name);
    return true;
}

google::protobuf::Service* TinyPbDispatcher::findService(const std::string& serviceName) const
{
    auto it = m_serviceMap.find(serviceName);
    if (it == m_serviceMap.end()) {
        return nullptr;
    }
    return it->second.get();
}

const google::protobuf::MethodDescriptor* TinyPbDispatcher::findMethod(
    google::protobuf::Service* service,
    const std::string& methodName) const
{
    if (service == nullptr) {
        return nullptr;
    }

    // [第三方 API] GetDescriptor() 返回 ServiceDescriptor，
    // FindMethodByName() 在其中按方法名查找 MethodDescriptor。
    return service->GetDescriptor()->FindMethodByName(methodName);
}

// TinyPbDispatcher 的核心分发函数，负责将解码后的 TinyPbStruct 请求路由到对应的 Protobuf 服务方法，
// 执行业务逻辑后将响应序列化并写回连接。
// 流程：解析服务名 → 查找 Service → 查找 Method → 反序列化 pbData →
//       CallMethod 执行业务 → 序列化响应 → 通过 sendProtocolData 写回连接。
// 任一步骤失败都会构造带错误码的响应发回客户端。
void TinyPbDispatcher::dispatch(AbstractData *data, TcpConnection *conn)
{
    if (data == nullptr || conn == nullptr) {
        return;
    }

    // dynamic_cast 将 AbstractData* 安全转为 TinyPbStruct*；
    // 若 data 实际类型不是 TinyPbStruct 则返回 nullptr，跳过处理。
    auto *request = dynamic_cast<TinyPbStruct *>(data);
    if (request == nullptr) {
        return;
    }

    // 构造响应骨架：保留请求的 reqId 和 serviceFullName，
    // 后续根据查找结果填充 errCode 和 errInfo。
    TinyPbStruct reply;
    reply.m_reqId = request->m_reqId;
    reply.m_serviceFullName = request->m_serviceFullName;

    // 第一步：解析 serviceFullName
    std::string serviceName;
    std::string methodName;
    if (!parseServiceFullName(request->m_serviceFullName, serviceName, methodName)) {
        reply.m_errCode = ERROR_PARSE_SERVICE_NAME;
        reply.m_errInfo = "failed to parse serviceFullName: " + request->m_serviceFullName;
        conn->sendProtocolData(&reply);
        return;
    }

    // 第二步：查找已注册的 Service
    google::protobuf::Service *service = findService(serviceName);
    if (service == nullptr) {
        reply.m_errCode = ERROR_SERVICE_NOT_FOUND;
        reply.m_errInfo = "service not found: " + serviceName;
        conn->sendProtocolData(&reply);
        return;
    }

    // 第三步：查找 MethodDescriptor
    const google::protobuf::MethodDescriptor *method = findMethod(service, methodName);
    if (method == nullptr) {
        reply.m_errCode = ERROR_METHOD_NOT_FOUND;
        reply.m_errInfo = "method not found: " + methodName;
        conn->sendProtocolData(&reply);
        return;
    }

    RequestContextGuard contextGuard(
        request->m_reqId,
        request->m_serviceFullName,
        "local",
        "peer"
    );

    // 第四步：找到 service + method，执行真正的 RPC 调用链路：
    //   pbData → ParseFromString → CallMethod → SerializeToString → pbData

    // [第三方 API] GetRequestPrototype / GetResponsePrototype 根据 MethodDescriptor
    // 返回对应的 Message 原型对象，New() 创建具体实例。
    // 使用 unique_ptr 管理生命周期，避免手写 delete。
    std::unique_ptr<google::protobuf::Message> rpcRequest(
        service->GetRequestPrototype(method).New());
    std::unique_ptr<google::protobuf::Message> rpcResponse(
        service->GetResponsePrototype(method).New());

    // 将 TinyPbStruct::m_pbData 反序列化为 Protobuf 请求消息。
    // [第三方 API] ParseFromString 失败说明 pbData 不是合法的序列化数据。
    if (!rpcRequest->ParseFromString(request->m_pbData)) {
        reply.m_errCode = ERROR_FAILED_DESERIALIZE;
        reply.m_errInfo = "failed to deserialize request pbData";
        conn->sendProtocolData(&reply);
        return;
    }

    // 构造 RpcController 并调用 Service::CallMethod()。
    // [第三方 API] CallMethod 是 google::protobuf::Service 提供的统一调用入口，
    // 内部根据 MethodDescriptor 分发到具体的方法实现（如 EchoService::echo）。
    // 参数：method（方法描述符）、controller（错误状态控制器）、
    //       request（反序列化后的请求对象）、response（待填充的响应对象）、
    //       done（完成回调闭包，当前阶段传 nullptr，不使用异步通知）。
    TinyPbRpcController controller;
    service->CallMethod(method, &controller, rpcRequest.get(), rpcResponse.get(), nullptr);

    // [第三方 API] Failed() 是 google::protobuf::RpcController 提供的方法，
    // 检查业务方法是否通过 SetFailed() 报告了错误。
    if (controller.Failed()) {
        reply.m_errCode = -1;
        // [第三方 API] ErrorText() 是 google::protobuf::RpcController 提供的方法，
        // 返回 SetFailed() 设置的错误描述文本。
        reply.m_errInfo = controller.ErrorText();
        conn->sendProtocolData(&reply);
        return;
    }

    // 将 Protobuf 响应消息序列化为二进制数据，写入 TinyPbStruct::m_pbData。
    // [第三方 API] SerializeToString 将 Protobuf 响应消息序列化为二进制数据。
    if (!rpcResponse->SerializeToString(&reply.m_pbData)) {
        reply.m_errCode = ERROR_FAILED_SERIALIZE;
        reply.m_errInfo = "failed to serialize response pbData";
        conn->sendProtocolData(&reply);
        return;
    }

    reply.m_errCode = 0;
    reply.m_errInfo = "";

    conn->sendProtocolData(&reply);
}

bool TinyPbDispatcher::parseServiceFullName(
    const std::string& fullName,
    std::string& serviceName,
    std::string& methodName) const
{
    if (fullName.empty()) {
        return false;
    }

    // 以第一个 '.' 为分隔，左侧为服务名，右侧为方法名。
    // 例如 "QueryService.query_name" → serviceName="QueryService", methodName="query_name"。
    auto dotPos = fullName.find('.');
    if (dotPos == std::string::npos) {
        return false;
    }

    serviceName = fullName.substr(0, dotPos);
    methodName = fullName.substr(dotPos + 1);

    // 拆分后任一部分为空也视为非法，例如 ".method" 或 "Service."。
    if (serviceName.empty() || methodName.empty()) {
        return false;
    }

    return true;
}

}
