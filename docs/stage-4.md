# 阶段 4：协议编解码接口准备

## 目标

阶段 4 在阶段 3（协程与 IO Hook）的基础上抽象出协议数据对象和编解码器接口，为后续 HTTP / TinyPB 等具体协议提供统一入口。本阶段定义接口基类、枚举以及 TinyPB 协议数据结构。

## 当前进度

| 任务 | 状态 | 说明 |
|------|------|------|
| 任务二十五：抽象协议数据和协议编解码接口 | 已完成 | `AbstractData` / `AbstractCodec` 接口基类；`ProtocolType` 枚举；`test_abstract_codec` 测试通过。 |
| 任务二十六：定义 TinyPB 协议数据结构 | 已完成 | `TinyPbStruct` 数据结构；成员变量命名统一为 `m_` 前缀；`test_tinypb_data` 测试通过。 |

## 任务二十五记录

任务二十五完成的目标是为协议层铺设统一的接口基类，让后续 HTTP / TinyPB 等具体协议都有统一入口：

- 新增 `mytinyrpc/net/abstractdata.h`：
  - `ProtocolType` 枚举（`TinyPb = 1`、`Http = 2`）。
  - `AbstractData` 类：`Ptr` 类型别名、`m_encodeSucc` / `m_decodeSucc` 状态标记、虚析构函数。
- 新增 `mytinyrpc/net/abstractcodec.h`：
  - `AbstractCodec` 类：`Ptr` 类型别名、纯虚 `encode()` / `decode()` / `getProtocolType()`。
  - `encode(TcpBuffer*, AbstractData*)`：将结构化数据序列化并追加到 `TcpBuffer`。
  - `decode(TcpBuffer*, AbstractData*)`：从 `TcpBuffer` 读取字节流并反序列化到数据对象。
- 两个文件均为纯头文件，无 `.cc` 实现。
- 新增 `testcases/test_abstract_codec.cc`（GoogleTest）：
  - 测试专用 `StringData : public AbstractData` 和 `StringCodec : public AbstractCodec`。
  - 四个用例：默认状态、encode 写入 buffer、decode 从 buffer 读取、`getProtocolType()` 返回值。
- `CMakeLists.txt` 新增 `test_abstract_codec` 可执行目标。
- 不涉及粘包/半包/协议解析、不接入 `TcpConnection`、不改变 Echo Server 行为。

## 任务二十六记录

任务二十六完成的目标是定义 TinyPB 协议的数据承载结构体，为后续 `TinyPbCodec` 编解码做准备：

- 新增 `mytinyrpc/net/tinypb/tinypbdata.h`（纯头文件）：
  - `TinyPbStruct : public AbstractData`，包含 TinyPB 协议全部字段：
    - `m_pkLen`：完整包长度。
    - `m_msgReq` / `m_msgReqLen`：请求号及其长度。
    - `m_serviceFullName` / `m_serviceNameLen`：服务完整名及其长度。
    - `m_errCode`：RPC 错误码。
    - `m_errInfo` / `m_errInfoLen`：错误信息及其长度。
    - `m_pbData`：序列化后的 protobuf 业务数据。
    - `m_checkNum`：校验字段。
  - 所有成员变量使用 `m_` 前缀，遵循编码规范。
- 同步修正 `AbstractData` 中 `encodeSucc` / `decodeSucc` 为 `m_encodeSucc` / `m_decodeSucc`，
  并更新 `abstractcodec.h` 注释和 `test_abstract_codec.cc` 测试代码。
- 新增 `testcases/test_tinypb_data.cc`（GoogleTest）：
  - 四个用例：默认值验证、基类指针兼容性、字段赋值读回、状态标记继承可见性。
- `CMakeLists.txt` 新增 `test_tinypb_data` 可执行目标。
- 不实现 `TinyPbCodec`、不定义起止字节、不做网络字节序转换、不处理半包/粘包。
