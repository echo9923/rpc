# 阶段 4：协议编解码接口准备

## 目标

阶段 4 在阶段 3（协程与 IO Hook）的基础上抽象出协议数据对象和编解码器接口，为后续 HTTP / TinyPB 等具体协议提供统一入口。本阶段定义接口基类、枚举以及 TinyPB 协议数据结构。

## 当前进度

| 任务 | 状态 | 说明 |
|------|------|------|
| 任务二十五：抽象协议数据和协议编解码接口 | 已完成 | `AbstractData` / `AbstractCodec` 接口基类；`ProtocolType` 枚举；`test_abstract_codec` 测试通过。 |
| 任务二十六：定义 TinyPB 协议数据结构 | 已完成 | `TinyPbStruct` 数据结构；成员变量命名统一为 `m_` 前缀；`test_tinypb_data` 测试通过。 |
| 任务二十七：实现 TinyPB 编码器的最小 encode 路径 | 已完成 | `TinyPbCodec::encode` 完整帧编码；网络字节序；错误校验；`test_tinypb_codec` 测试通过。 |
| 任务二十八：实现 TinyPB 解码器的完整单包 decode 路径 | 已完成 | `TinyPbCodec::decode` 单包解析；网络字节序还原；失败不消费 buffer；`test_tinypb_codec` 测试通过。 |
| 任务二十九：增强 TinyPB decode 的流式拆包边界处理 | 已完成 | `TinyPbCodec::decode` 前置噪音跳过；半包保留；粘包单次消费一帧；`findFrameStart` 辅助；`test_tinypb_codec` 测试通过。 |

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

## 任务二十七记录

任务二十七完成的目标是实现 TinyPB 协议编码器的最小 encode 路径，将 `TinyPbStruct` 结构体转换为符合协议规范的字节流：

- 新增 `mytinyrpc/net/tinypb/tinypbcodec.h` / `tinypbcodec.cc`：
  - `TinyPbCodec : public AbstractCodec`。
  - 起止字节常量 `kTinyPbStart = 0x02`、`kTinyPbEnd = 0x03`。
  - `encode()`：前置校验（nullptr、类型转换、必填字段非空）→ 回填长度字段 → 计算 `m_pkLen` → 按协议布局顺序写入 `TcpBuffer`。
  - 私有辅助 `appendInt32()`：通过 `htonl` 将 int32_t 转为网络字节序后追加 4 字节到 buffer。
  - `decode()`：空实现，仅设 `m_decodeSucc = false`。
  - `getProtocolType()`：返回 `ProtocolType::TinyPb`。
  - `m_checkNum` 固定为 `1`，暂不实现真实校验算法。
- 新增 `testcases/test_tinypb_codec.cc`（GoogleTest）：
  - 五个用例：协议类型、完整帧验证（起止字节/字段顺序/内容）、网络字节序验证、回填字段验证、非法输入拒绝。
- `CMakeLists.txt` 新增 `tinypbcodec.cc` 到 SRC 列表，新增 `test_tinypb_codec` 可执行目标。
- 不实现 decode 真实逻辑、不处理半包/粘包、不接入 `TcpConnection`、不改变 Echo Server 行为。

## 任务二十八记录

任务二十八完成的目标是实现 TinyPB 协议解码器的完整单包 decode 路径，将 `TcpBuffer` 中的字节流反序列化为 `TinyPbStruct`：

- 修改 `mytinyrpc/net/tinypb/tinypbcodec.h`：
  - 新增私有辅助 `readInt32()`：从指定偏移处读取 4 字节网络序 int32_t 并转为主机序。
  - 更新 `decode()` 注释。
- 修改 `mytinyrpc/net/tinypb/tinypbcodec.cc`：
  - `decode()` 完整实现：前置校验（nullptr、类型转换）→ 最小帧长度检查 → 起始符校验 → 读取 pkLen → 半包检查 → 结束符校验 → 依次解析全部字段 → 成功时 `m_decodeSucc = true` 并 `buffer->retrieve(pkLen)`。
  - `readInt32()` 实现：边界检查 + `ntohl` 转换。
  - `pbData` 长度通过 `pkLen - 已解析字节数 - 4(checkNum) - 1(PB_END)` 推导。
  - 失败时 `m_decodeSucc = false`，不调用 `buffer->retrieve()`。
- 修改 `testcases/test_tinypb_codec.cc`：
  - 新增五个 decode 用例：encode/decode 往返验证、网络字节序字段解析、不完整帧拒绝、篡改起止符拒绝、非法数据类型拒绝。
- 不扫描脏数据、不循环解析多包、不做 checksum 校验、不接入 `TcpConnection`、不改 Echo Server 行为。

## 任务二十九记录

任务二十九完成的目标是增强 `TinyPbCodec::decode()` 的流式拆包边界处理，使其能应对真实 TCP 字节流中的前置杂音、半包和粘包场景：

- 修改 `mytinyrpc/net/tinypb/tinypbcodec.h`：
  - 新增私有辅助 `findFrameStart()`：在可读区间内查找第一个 `kTinyPbStart` (0x02)。
  - 更新 `decode()` 注释，说明前置扫描、半包保留、单次单帧等行为。
- 修改 `mytinyrpc/net/tinypb/tinypbcodec.cc`：
  - `decode()` 入口处调用 `findFrameStart()` 扫描起始符，后续所有校验和字段解析基于帧起点（`frameRaw`、`frameReadable`）进行。
  - 成功时 `buffer->retrieve(startPos + pkLen)` 一次性消费前置无效字节 + 完整帧。
  - 失败时不调用 `retrieve`，保留全部数据（前置噪音 + 半包），等待更多数据到来。
  - 一次 decode 只解析第一个完整帧，后续帧保留在 buffer 中供下次 decode。
  - `findFrameStart()` 实现为线性扫描，不做坏包跳过恢复策略。
- 修改 `testcases/test_tinypb_codec.cc`：
  - 新增五个流式拆包测试用例：前置噪音跳过、粘包单帧消费、连续两次 decode 两帧、半包追加后成功、噪音+半包不消费。
  - 所有任务二十七/二十八已有测试不退化。
- 不接入 `TcpConnection`、不改 Echo Server、不做 checksum、不做坏包恢复、不做最大包长限制。
