#pragma once

#include "net/abstractdispatcher.h"

#include <google/protobuf/descriptor.h>
#include <google/protobuf/service.h>

#include <map>
#include <memory>
#include <string>

namespace tinyrpc {

// TinyPbDispatcher 负责 TinyPB 协议的分发逻辑。
// 内部维护一个服务注册表（m_serviceMap），以 Protobuf Service 的 full_name 为 key。
//
// dispatch() 流程：
//   1. 解析 serviceFullName → serviceName + methodName
//   2. 在 m_serviceMap 中查找对应的 Service
//   3. 通过 Service::GetDescriptor() 查找 MethodDescriptor
//   4. 反序列化 pbData → CallMethod → 序列化 response → pbData
//   5. 任一步骤失败则构造带错误码的响应
class TinyPbDispatcher : public AbstractDispatcher {
 public:
    using Ptr = std::shared_ptr<TinyPbDispatcher>;
    // ServicePtr 是 Protobuf 服务对象的智能指针别名。
    // [第三方 API] google::protobuf::Service 是 Protobuf 编译器生成的服务基类，
    // 用户实现的具体服务（如 EchoService）均继承自该基类。
    using ServicePtr = std::shared_ptr<google::protobuf::Service>;

    // 处理解码后的 TinyPbStruct 请求，查找服务和方法后构造响应并写回连接。
    // 查找失败时响应的 m_errCode 和 m_errInfo 会填入对应的错误信息。
    void dispatch(AbstractData *data, TcpConnection *conn) override;

    // 将 "ServiceName.methodName" 形式的完整服务名拆分为两部分。
    // fullName：完整服务名，如 "QueryService.query_name"。
    // serviceName：输出参数，拆分后的服务名部分。
    // methodName：输出参数，拆分后的方法名部分。
    // 返回值：拆分成功返回 true；fullName 为空、不含 '.'、
    // 或拆分后任一部分为空时返回 false。
    bool parseServiceFullName(
        const std::string& fullName,
        std::string& serviceName,
        std::string& methodName
    ) const;

    // 注册一个 Protobuf Service 到分发器。
    // 以 service->GetDescriptor()->full_name() 为 key 存入注册表。
    // 重复注册同一服务名时返回 false，不覆盖已有注册。
    bool registerService(ServicePtr service);

    // 按服务名查找已注册的 Protobuf Service。
    // [第三方 API] 返回值 google::protobuf::Service* 是 Protobuf 服务实例的裸指针，
    // 生命周期由 m_serviceMap 中的 shared_ptr 管理。
    // 找不到时返回 nullptr。
    google::protobuf::Service* findService(const std::string& serviceName) const;

    // 在指定 Service 中按方法名查找 MethodDescriptor。
    // [第三方 API] google::protobuf::MethodDescriptor 是 Protobuf 反射 API 中的类型，
    // 描述服务中某个 RPC 方法的元信息（方法名、输入/输出消息类型等）。
    // 找不到时返回 nullptr。
    const google::protobuf::MethodDescriptor* findMethod(
        google::protobuf::Service* service,
        const std::string& methodName
    ) const;

 private:
    // 服务注册表：key = Service::GetDescriptor()->full_name()，value = Service 智能指针。
    std::map<std::string, ServicePtr> m_serviceMap;
};

}
