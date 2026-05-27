# 阶段 4：协议编解码接口准备

## 目标

阶段 4 在阶段 3（协程与 IO Hook）的基础上抽象出协议数据对象和编解码器接口，为后续 HTTP / TinyPB 等具体协议提供统一入口。本阶段只定义接口基类和枚举，不实现任何具体协议。

## 当前进度

| 任务 | 状态 | 说明 |
|------|------|------|
| 任务二十五：抽象协议数据和协议编解码接口 | 已完成 | `AbstractData` / `AbstractCodec` 接口基类；`ProtocolType` 枚举；`test_abstract_codec` 测试通过。 |

## 任务二十五记录

任务二十五完成的目标是为协议层铺设统一的接口基类，让后续 HTTP / TinyPB 等具体协议都有统一入口：

- 新增 `mytinyrpc/net/abstractdata.h`：
  - `ProtocolType` 枚举（`TinyPb = 1`、`Http = 2`）。
  - `AbstractData` 类：`Ptr` 类型别名、`encodeSucc` / `decodeSucc` 状态标记、虚析构函数。
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
