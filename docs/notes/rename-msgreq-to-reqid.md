# MsgReq -> ReqId 命名清理（已实施）

`MsgReq`（Message Request）这个命名在代码中表示“请求号”，但读起来像“消息请求”，尤其 `genMsgReq()` 完全看不出是在生成一个编号。

## 已实施改名方案

| 原名 | 新名 | 说明 |
|---|---|---|
| `MsgReqUtil` | `ReqIdUtil` | 工具类 |
| `genMsgNumber()` | `genReqId()` | 生成请求号 |
| `genMsgReq()` | `genReqId()` | Channel 的请求号生成方法 |
| `setMsgReqGenerator()` | `setReqIdGenerator()` | 注入请求号生成器 |
| `m_msgReqGenerator` | `m_reqIdGenerator` | 请求号生成器成员 |
| `setMsgReq()` / `getMsgReq()` | `setReqId()` / `getReqId()` | Controller 存取器，按当前编码规范使用 lowerCamelCase/get 前缀 |
| `m_msgReq` | `m_reqId` | 请求号成员 |
| `m_msgReqLen` | `m_reqIdLen` | 请求号长度 |
| `ERROR_RPC_MSGREQ_MISMATCH` | `ERROR_RPC_REQID_MISMATCH` | 请求号不匹配错误码 |
| `comm/msgreq.h/cc` | `comm/reqid.h/cc` | 文件名 |
| `test_msg_req.cc` | `test_req_id.cc` | 测试文件名 |

## 实施范围

已同步源码、测试、CMake、验收脚本、生成器模板和相关当前说明文档。改名仅调整代码命名和日志展示术语，TinyPB 线协议字段顺序保持不变。
