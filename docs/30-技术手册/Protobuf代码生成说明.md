# Protobuf 代码生成说明

本文档说明 `protoc --cpp_out=...` 针对 `.proto` 文件会自动生成哪些 C++ 代码，并以本项目 `examples/hello_rpc/proto/hello_rpc.proto` 为例逐项对照。阅读本文前可先浏览同目录的 [protobuf-rpc-guide.md](./protobuf-rpc-guide.md)，那一份侧重"项目如何使用 protobuf 实现 RPC"，本文则聚焦"生成代码清单与对应关系"。

---

## 一、生成的文件总览

对每一个 `.proto`，`protoc` 在 `--cpp_out` 指定的目录下生成两个文件：

```
hello_rpc.proto
    │  protoc --cpp_out=<dir> hello_rpc.proto
    ▼
hello_rpc.pb.h     # 头文件：所有类 / 枚举 / 服务声明
hello_rpc.pb.cc    # 源文件：方法实现 + 全局描述符注册
```

本仓库 `build/` 目录下的 `e2e_rpc.pb.cc`、`test_tinypb_server.pb.cc` 等都是 CMake 在构建阶段自动调用 `protoc` 生成的产物。当 `.proto` 变更后，重新构建即可刷新这两个文件。

---

## 二、对每个 `message` 生成的类

对 `.proto` 中每一个 `message`，protoc 生成一个同名 C++ 类，继承自 `google::protobuf::Message`。以 `hello_rpc.proto` 中的 `HelloReq` 为例：

```proto
message HelloReq {
  int32 id = 1;
  string name = 2;
}
```

将生成 `class HelloReq`，围绕"每一个字段"自动产出一组方法。`HelloRes` 同理。

### 2.1 字段访问器

| 类别 | 生成的方法（以 `id` / `name` 字段为例） |
|------|------------------------------------------|
| 读访问器 | `int32_t id() const`、`const std::string& name() const` |
| 写访问器 | `void set_id(int32_t)`、`void set_name(const std::string&)`、`void set_name(std::string&&)`、`void set_name(const char*)` |
| 可变指针 | `std::string* mutable_name()`（拿到可直接修改的指针） |
| 清空 | `void clear_id()`、`void clear_name()` |
| 存在性 | proto3 标量默认不生成 `has_*`；声明为 `optional` 的字段会生成 `bool has_xxx() const` |
| repeated 专用 | `int xxx_size() const`、`add_xxx()`、`xxx(int index)` 等 |

本项目 `examples/hello_rpc/tinypb_server.cc` 的 `HelloServiceImpl::hello` 中调用的 `request->name()`、`request->id()`、`response->set_ret_code()`、`response->set_res_info()`、`response->set_message()`，**全部是 protoc 自动生成的访问器**，业务代码本身并没有手写它们。

### 2.2 序列化与反序列化

这些方法继承自 `google::protobuf::Message`，是 RPC 框架传输层把对象转成字节流、再还原回来的基础：

```cpp
// 序列化：对象 -> 字节流
bool SerializeToString(std::string* output) const;
bool SerializeToArray(const void* data, int size) const;
std::size_t ByteSizeLong() const;   // 序列化后的字节数

// 反序列化：字节流 -> 对象
bool ParseFromString(const std::string& data);
bool ParseFromArray(const void* data, int size);
```

框架的 `TinyPbCodec` 正是借助这些方法完成请求/响应的打包与拆包。

---

## 三、对 `service` 生成的代码（RPC 专用）

只有当 `.proto` 中开启

```proto
option cc_generic_services = true;
```

时，`service` 才会生成 C++ 代码。**不开这个选项，`service HelloService` 不会生成任何东西。** 本项目所有 tinypb 协议的 `.proto` 都必须开启它。

开启后，protoc 为 `service HelloService { rpc hello(HelloReq) returns (HelloRes); }` 生成以下三类代码：

### 3.1 抽象基类 `HelloService`（服务端继承它）

```cpp
class HelloService : public google::protobuf::Service {
 public:
  virtual void hello(google::protobuf::RpcController* controller,
                     const HelloReq* request,
                     HelloRes* response,
                     google::protobuf::Closure* done) = 0;  // 纯虚，业务去实现
  // ...
};
```

项目里 `class HelloServiceImpl : public HelloService` 继承的就是它，`override hello(...)` 重写的虚函数也是它。

### 3.2 客户端存根 `HelloService_Stub`（客户端用它发起调用）

```cpp
class HelloService_Stub : public HelloService {
 public:
  explicit HelloService_Stub(google::protobuf::RpcChannel* channel);
  void hello(google::protobuf::RpcController* controller,
             const HelloReq* request,
             HelloRes* response,
             google::protobuf::Closure* done) override;
};
```

`Stub` 把"调用 `hello`"翻译成：序列化请求 → 通过 `RpcChannel` 发出 → 等待响应或触发回调。`examples/hello_rpc/tinypb_client.cc` 里同步/异步两路调用都依赖这个 `Stub`。

### 3.3 服务端分派用的元信息（让框架能按方法名转发请求）

```cpp
// service 的描述符，含 service 名、所有方法名
static const google::protobuf::ServiceDescriptor* descriptor();

// 框架收到请求后，按方法名调用对应虚函数
virtual void CallMethod(const google::protobuf::MethodDescriptor* method,
                        google::protobuf::RpcController* controller,
                        const google::protobuf::Message* request,
                        google::protobuf::Message* response,
                        google::protobuf::Closure* done);

// 取请求/响应原型，用于反射式 new 出空对象
virtual const google::protobuf::Message& GetRequestPrototype(
    const google::protobuf::MethodDescriptor* method) const;
virtual const google::protobuf::Message& GetResponsePrototype(
    const google::protobuf::MethodDescriptor* method) const;
```

框架的 `REGISTER_SERVICE` 与 `TinyPbDispatcher` 分发逻辑，正是借助 `descriptor()`、`CallMethod` 把网络层收到的"方法名 + 字节流"路由到用户写的 `HelloServiceImpl::hello`。这就是为什么 `cc_generic_services = true` 对本项目是硬性前提。

---

## 四、元信息与全局注册代码

无论有没有 `service`，每个 `*.pb.cc` 末尾都会生成一些"登记"性质的代码：

- **Descriptor（描述符）**：记录每个 message / service / 字段 / 枚举的名称、编号、类型等结构信息，供 `CallMethod`、反射、debug 打印使用。
- **Reflection（反射）**：允许通过字段名或字段编号动态读写字段，框架若需要做泛型处理时会用到。
- **静态初始化 / `InitDefaults`**：把本 proto 的描述符注册到 protobuf 全局类型表，保证 `new HelloReq()` 能正确初始化。
- **内部函数**（如 `protobuf_AddDesc_hello_rpc_2eproto`）：在程序启动阶段把本 proto 的描述符注册进全局表。

这些通常不需要业务关心，但在排查"类型未注册""descriptor pool 找不到"等问题时会涉及。

---

## 五、对照速查表

以 `examples/hello_rpc/proto/hello_rpc.proto` 为例：

| `.proto` 中写的内容 | protoc 生成的 C++ | 项目里用在哪里 |
|---|---|---|
| `message HelloReq { int32 id = 1; ... }` | `class HelloReq` + `id()/set_id()/clear_id()` + 序列化方法 | 承载请求数据 |
| `message HelloRes { ... }` | `class HelloRes` + 对应访问器 + 序列化方法 | 承载响应数据 |
| `service HelloService { rpc hello(...) }` + `cc_generic_services = true` | `class HelloService`（抽象基类）、`HelloService_Stub`、`descriptor()`、`CallMethod`、`GetRequestPrototype/GetResponsePrototype` | 服务端实现、客户端调用、框架分发 |

---

## 六、小结

`protoc --cpp_out` 对每个 `.proto` 生成 `*.pb.h` / `*.pb.cc`，内容可归为三类：

1. **每个 `message` → 一个数据类**：字段访问器（getter/setter/mutable/clear）+ 序列化/反序列化方法。
2. **每个 `service`（需 `cc_generic_services = true`）→ 三件套**：抽象基类（服务端继承）、客户端 `Stub`（客户端调用）、分派元信息 `descriptor()/CallMethod/...`（框架按方法名转发）。
3. **`.pb.cc` 末尾的描述符、反射与全局注册代码**：维护 protobuf 类型表，支撑运行时反射与分发。

本项目的 RPC 框架（`TinyPbCodec`、`TinyPbDispatcher`、`TinyPbRpcChannel`、`REGISTER_SERVICE` 等）正是把上面这些生成代码粘合进网络层，从而跑通"客户端 Stub → 序列化 → TCP 传输 → 服务端分发 → Service::CallMethod → 业务实现"的完整调用链。
