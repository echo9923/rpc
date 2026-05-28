#include "net/tinypb/tinypbdispatcher.h"

#include "net/tcpconnection.h"
#include "net/tinypb/tinypbdata.h"

#include <string>

namespace tinyrpc {

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

    // 解析 serviceFullName，当前阶段仅做合法性校验，
    // 后续接入服务注册表后用于查找对应的 Service 和 Method。
    std::string serviceName;
    std::string methodName;
    parseServiceFullName(request->m_serviceFullName, serviceName, methodName);

    // 构造最小响应：保留请求的 msgReq、serviceFullName、pbData，
    // 不做真正的业务处理，仅验证 dispatcher 路径可通。
    TinyPbStruct reply;
    reply.m_msgReq = request->m_msgReq;
    reply.m_serviceFullName = request->m_serviceFullName;
    reply.m_pbData = request->m_pbData;
    reply.m_errCode = 0;
    reply.m_errInfo = "";

    // 通过连接的 sendProtocolData() 将响应编码后写入输出缓冲区，
    // 由后续的 output() 阶段通过 write_hook 发送到 socket。
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
