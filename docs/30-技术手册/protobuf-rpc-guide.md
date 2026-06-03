# Protobuf RPC 技术手册

本文档详细说明项目中如何使用 Google Protobuf 框架实现自定义 RPC，涵盖 Protobuf 提供的核心 API、当前项目中已使用和未使用的部分，以及后续需要自行完成的工作。

---

## 一、整体架构概览

本项目的 RPC 实现不依赖 gRPC，而是基于 `cc_generic_services = true` 让 protoc 生成同步服务基类，再自行实现传输协议（TinyPB）、编解码、分发、控制器等组件。

```
┌─────────────────────────────────────────────────────────┐
│                      .proto 文件                         │
│  定义 message + service，cc_generic_services = true      │
└──────────────────────┬──────────────────────────────────┘
                       │ protoc 编译
                       ▼
┌─────────────────────────────────────────────────────────┐
│              protoc 生成的代码（第三方提供）               │
│  ┌─────────────┐  ┌──────────────┐  ┌────────────────┐ │
│  │ Message 子类 │  │ Service 基类  │  │ Descriptor 元信息│ │
│  │ (请求/响应)  │  │ (虚函数骨架)  │  │ (反射 API)     │ │
│  └─────────────┘  └──────────────┘  └────────────────┘ │
└──────────────────────┬──────────────────────────────────┘
                       │ 继承/调用
                       ▼
┌─────────────────────────────────────────────────────────┐
│              项目自行实现的部分                            │
│  ┌──────────────────┐  ┌─────────────────────────────┐  │
│  │ TinyPbRpcController│  │ TinyPbDispatcher (服务注册/分发)│  │
│  └──────────────────┘  └─────────────────────────────┘  │
│  ┌──────────────────┐  ┌─────────────────────────────┐  │
│  │ TinyPbCodec (编解码)│ │ TcpServer / TcpConnection    │  │
│  └──────────────────┘  └─────────────────────────────┘  │
│  ┌──────────────────┐  ┌─────────────────────────────┐  │
│  │ TinyPbStruct (协议)│ │ 业务 Service 实现 (用户代码)  │  │
│  └──────────────────┘  └─────────────────────────────┘  │
└─────────────────────────────────────────────────────────┘
```

---

## 二、Protobuf 提供的核心 API

### 2.1 Message API（消息操作）

Protobuf 编译器为每个 `.proto` 中定义的 `message` 生成一个 C++ 类，继承自 `google::protobuf::Message`。

#### 2.1.1 字段访问

```cpp
// .proto 定义：
// message queryNameReq {
//   int32 req_no = 1;
//   int32 id = 2;
// }

queryNameReq req;
req.set_req_no(42);       // 设置字段值
req.set_id(100);
int32_t no = req.req_no();  // 读取字段值
bool has = req.has_req_no(); // 检查字段是否设置（proto3 中标量类型始终有默认值）
req.clear_req_no();         // 清除字段，恢复为默认值
```

| 方法模式 | 说明 | 适用类型 |
|---------|------|---------|
| `set_xxx(value)` | 设置字段值 | 标量、string |
| `xxx()` | 获取字段值 | 标量、string |
| `mutable_xxx()` | 获取可变指针，用于嵌套消息 | message 类型 |
| `has_xxx()` | 字段是否被显式设置 | optional 字段、message 字段 |
| `clear_xxx()` | 清除字段 | 所有类型 |
| `add_xxx()` | 追加 repeated 字段元素 | repeated |
| `xxx_size()` | repeated 字段元素数量 | repeated |

#### 2.1.2 序列化与反序列化

```cpp
// 序列化：Message → 二进制字符串
queryNameReq req;
req.set_req_no(1);
std::string binary;
req.SerializeToString(&binary);       // 序列化为二进制（紧凑格式）
req.SerializeToArray(buf, bufSize);   // 序列化到指定缓冲区

// 反序列化：二进制 → Message
queryNameReq req2;
req2.ParseFromString(binary);         // 从二进制字符串解析
req2.ParseFromArray(buf, bufSize);    // 从缓冲区解析
```

> **项目使用场景**：`TinyPbDispatcher::dispatch()` 中通过 `ParseFromString()` 将 `TinyPbStruct::m_pbData` 反序列化为请求消息，通过 `SerializeToString()` 将响应消息序列化回 `m_pbData`。

#### 2.1.3 动态创建消息（反射）

```cpp
// 根据 MethodDescriptor 动态创建请求/响应实例
google::protobuf::Message* request =
    service->GetRequestPrototype(method).New();   // 创建请求消息
google::protobuf::Message* response =
    service->GetResponsePrototype(method).New();  // 创建响应消息
```

| API | 所属类 | 说明 |
|-----|--------|------|
| `GetRequestPrototype(method)` | `Service` | 返回方法入参的消息原型 |
| `GetResponsePrototype(method)` | `Service` | 返回方法出参的消息原型 |
| `New()` | `Message` | 创建同类型的新实例（堆分配） |

> **项目使用场景**：`TinyPbDispatcher::dispatch()` 中根据 `MethodDescriptor` 动态创建消息实例，避免在 dispatcher 中硬编码具体的消息类型。

#### 2.1.4 其他常用 Message API

```cpp
// 消息类型信息
const google::protobuf::Descriptor* desc = req.GetDescriptor();  // 类型描述符
std::string typeName = desc->full_name();  // 类型全限定名
std::string json;
google::protobuf::util::MessageToJsonString(req, &json);  // 转为 JSON

// 消息大小
int byteSize = req.ByteSizeLong();  // 序列化后的字节大小

// 清空与判断
req.Clear();          // 清空所有字段
bool empty = req.IsInitialized();  // 所有 required 字段是否已设置（proto3 始终为 true）
```

---

### 2.2 Service API（服务定义与调用）

当 `.proto` 文件声明 `option cc_generic_services = true` 时，protoc 会为每个 `service` 生成一个 C++ 抽象基类。

#### 2.2.1 生成的 Service 基类

以 `QueryService` 为例，protoc 生成的代码大致等价于：

```cpp
class QueryService : public google::protobuf::Service {
public:
    // 纯虚函数：用户必须实现此方法来定义业务逻辑
    virtual void query_name(
        google::protobuf::RpcController* controller,
        const queryNameReq* request,
        queryNameRes* response,
        google::protobuf::Closure* done) = 0;

    // CallMethod：根据 MethodDescriptor 分发到具体方法
    // 用户通常不需要重写此方法
    void CallMethod(
        const google::protobuf::MethodDescriptor* method,
        google::protobuf::RpcController* controller,
        const google::protobuf::Message* request,
        google::protobuf::Message* response,
        google::protobuf::Closure* done) override;
};
```

#### 2.2.2 用户实现 Service

```cpp
class QueryServiceImpl : public QueryService {
public:
    void query_name(
        google::protobuf::RpcController* controller,
        const queryNameReq* request,
        queryNameRes* response,
        google::protobuf::Closure* done) override
    {
        // 1. 从 request 读取参数
        int32_t id = request->id();
        int32_t reqNo = request->req_no();

        // 2. 执行业务逻辑
        std::string name = lookupName(id);

        // 3. 填充 response
        response->set_ret_code(0);
        response->set_res_info("ok");
        response->set_req_no(reqNo);
        response->set_id(id);
        response->set_name(name);

        // 4. 如果传入了 done 回调，调用它通知框架处理完成
        if (done != nullptr) {
            done->Run();
        }

        // 或者通过 controller 报告错误：
        // controller->SetFailed("something went wrong");
    }
};
```

#### 2.2.3 Service 核心方法

| 方法 | 说明 |
|------|------|
| `CallMethod(method, controller, request, response, done)` | 根据 `MethodDescriptor` 自动分发到对应的虚函数实现 |
| `GetDescriptor()` | 返回 `ServiceDescriptor*`，获取服务的元信息 |
| `GetRequestPrototype(method)` | 返回方法入参的消息原型 |
| `GetResponsePrototype(method)` | 返回方法出参的消息原型 |

> **项目使用场景**：`TinyPbDispatcher::dispatch()` 通过 `CallMethod()` 统一调用已注册的服务方法，无需知道具体的服务类型。

---

### 2.3 Descriptor API（反射与元信息）

Descriptor 是 Protobuf 的反射系统核心，允许在运行时查询消息、服务、方法的元信息。

#### 2.3.1 ServiceDescriptor

```cpp
const google::protobuf::ServiceDescriptor* svcDesc =
    service->GetDescriptor();

svcDesc->full_name();        // "QueryService"
svcDesc->name();             // "QueryService"
svcDesc->method_count();     // 方法数量
svcDesc->method(0);          // 按索引获取 MethodDescriptor
svcDesc->FindMethodByName("query_name");  // 按名称查找方法
```

#### 2.3.2 MethodDescriptor

```cpp
const google::protobuf::MethodDescriptor* method =
    svcDesc->FindMethodByName("query_name");

method->name();              // "query_name"
method->full_name();         // "QueryService.query_name"
method->service();           // 返回所属 ServiceDescriptor
method->input_type();        // 返回入参 Message 的 Descriptor
method->output_type();       // 返回出参 Message 的 Descriptor
```

#### 2.3.3 Descriptor（消息元信息）

```cpp
const google::protobuf::Descriptor* msgDesc = req.GetDescriptor();

msgDesc->name();             // "queryNameReq"
msgDesc->full_name();        // "queryNameReq"
msgDesc->field_count();      // 字段数量
msgDesc->FindFieldByName("id");  // 按名称查找字段描述符
```

> **项目使用场景**：`registerService()` 中通过 `GetDescriptor()->full_name()` 获取服务全名作为注册表 key；`findMethod()` 中通过 `FindMethodByName()` 查找方法。

---

### 2.4 RpcController API（调用控制）

`google::protobuf::RpcController` 是一个纯虚接口，用于控制 RPC 调用的生命周期和传递错误状态。**Protobuf 只定义接口，不提供实现**——实现由 RPC 框架（即本项目）提供。

```cpp
class RpcController {
public:
    virtual void Reset() = 0;
    virtual bool Failed() const = 0;
    virtual std::string ErrorText() const = 0;
    virtual void SetFailed(const std::string& reason) = 0;
    virtual void StartCancel() = 0;
    virtual bool IsCanceled() const = 0;
    virtual void NotifyOnCancel(Closure* callback) = 0;
};
```

| 方法 | 说明 | 本项目实现状态 |
|------|------|---------------|
| `Reset()` | 重置状态 | 已实现 |
| `Failed()` | 查询是否失败 | 已实现 |
| `ErrorText()` | 获取错误描述 | 已实现 |
| `SetFailed(reason)` | 设置失败原因 | 已实现 |
| `StartCancel()` | 发起取消 | 空实现（预留） |
| `IsCanceled()` | 是否已取消 | 空实现（预留） |
| `NotifyOnCancel(callback)` | 注册取消回调 | 空实现（预留） |

> **项目使用场景**：`TinyPbRpcController` 继承此接口，在 `dispatch()` 中创建实例传给 `CallMethod()`，业务代码可通过 `SetFailed()` 报告错误。

---

### 2.5 Closure API（回调闭包）

```cpp
class google::protobuf::Closure {
public:
    virtual ~Closure();
    virtual void Run() = 0;
};
```

Protobuf 提供 `NewPermanentCallback()` 工厂函数创建 Closure：

```cpp
// 创建一次性回调（Run 后自动销毁）
google::protobuf::Closure* done =
    google::protobuf::NewCallback(&SomeFunction);

// 创建带参数的回调
google::protobuf::Closure* done =
    google::protobuf::NewCallback(&obj, &Class::Method, arg1);

// 创建永久回调（Run 后不销毁，需手动 delete）
google::protobuf::Closure* done =
    google::protobuf::NewPermanentCallback(&SomeFunction);
```

> **项目使用场景**：`CallMethod()` 的最后一个参数 `done` 即 Closure 类型。当前项目传 `nullptr`（同步模式），后续异步改造时需要传入。

---

### 2.6 CMake 集成 API

```cmake
# 查找系统中安装的 Protobuf
find_package(Protobuf REQUIRED)

# 关键变量
#   Protobuf_INCLUDE_DIRS  - 头文件路径
#   Protobuf_LIBRARIES     - 链接库
#   Protobuf_PROTOC_EXECUTABLE - protoc 编译器路径

# 自动编译 .proto 文件为 .pb.cc / .pb.h
protobuf_generate_cpp(PROTO_SRCS PROTO_HDRS
    testcases/test_tinypb_server.proto)

# 将生成的源文件加入编译
add_executable(myapp ${PROTO_SRCS})

# 链接 Protobuf 库
target_link_libraries(myapp ${Protobuf_LIBRARIES})

# 生成的头文件在 ${CMAKE_CURRENT_BINARY_DIR}，需要加入 include 路径
include_directories(${CMAKE_CURRENT_BINARY_DIR})
```

---

## 三、当前项目已使用的 Protobuf API 汇总

下表列出项目中实际调用的 Protobuf API 及其位置：

| API | 调用位置 | 用途 |
|-----|---------|------|
| `Service::GetDescriptor()` | [tinypbdispatcher.cc:24](../mytinyrpc/net/tinypb/tinypbdispatcher.cc) | 获取服务全名作为注册表 key |
| `ServiceDescriptor::full_name()` | [tinypbdispatcher.cc:24](../mytinyrpc/net/tinypb/tinypbdispatcher.cc) | 获取服务全名 |
| `ServiceDescriptor::FindMethodByName()` | [tinypbdispatcher.cc:60](../mytinyrpc/net/tinypb/tinypbdispatcher.cc) | 按方法名查找 MethodDescriptor |
| `Service::GetRequestPrototype().New()` | [tinypbdispatcher.cc:122](../mytinyrpc/net/tinypb/tinypbdispatcher.cc) | 动态创建请求消息实例 |
| `Service::GetResponsePrototype().New()` | [tinypbdispatcher.cc:124](../mytinyrpc/net/tinypb/tinypbdispatcher.cc) | 动态创建响应消息实例 |
| `Message::ParseFromString()` | [tinypbdispatcher.cc:128](../mytinyrpc/net/tinypb/tinypbdispatcher.cc) | 反序列化请求 |
| `Service::CallMethod()` | [tinypbdispatcher.cc:142](../mytinyrpc/net/tinypb/tinypbdispatcher.cc) | 分发到业务方法 |
| `RpcController::Failed()` | [tinypbdispatcher.cc:146](../mytinyrpc/net/tinypb/tinypbdispatcher.cc) | 检查是否出错 |
| `RpcController::ErrorText()` | [tinypbdispatcher.cc:150](../mytinyrpc/net/tinypb/tinypbdispatcher.cc) | 获取错误文本 |
| `Message::SerializeToString()` | [tinypbdispatcher.cc:157](../mytinyrpc/net/tinypb/tinypbdispatcher.cc) | 序列化响应 |
| `RpcController` 接口实现 | [tinypbrpccontroller.h](../mytinyrpc/net/tinypb/tinypbrpccontroller.h) | 实现控制器 |
| `Closure` 类型引用 | [tinypbrpccontroller.cc](../mytinyrpc/net/tinypb/tinypbrpccontroller.cc) | NotifyOnCancel 参数类型 |

---

## 四、完整的 RPC 数据流

```
客户端发送 TCP 字节流
    │
    ▼
TcpConnection::input()
    │  readHook 协程读取 socket 数据到 m_inputBuffer
    ▼
TcpConnection::execute()
    │
    ├─ TinyPbCodec::decode()
    │   │  扫描 0x02 起始标记 → 校验包长 → 解析各字段
    │   │  → 构造 TinyPbStruct（含 m_pbData）
    │   ▼
    ├─ TinyPbDispatcher::dispatch()
    │   │
    │   ├─ 1. parseServiceFullName("QueryService.query_name")
    │   │     → serviceName="QueryService", methodName="query_name"
    │   │
    │   ├─ 2. findService("QueryService")
    │   │     → m_serviceMap 查找 → 返回 Service*
    │   │
    │   ├─ 3. findMethod(service, "query_name")
    │   │     → ServiceDescriptor::FindMethodByName() → MethodDescriptor*
    │   │
    │   ├─ 4. GetRequestPrototype(method).New()
    │   │     → 动态创建 queryNameReq 实例
    │   │     ParseFromString(m_pbData)
    │   │     → 将二进制数据反序列化为结构化消息
    │   │
    │   ├─ 5. service->CallMethod(method, &controller, request, response, nullptr)
    │   │     → Protobuf 自动分发到 QueryServiceImpl::query_name()
    │   │     → 业务代码填充 response
    │   │
    │   ├─ 6. response->SerializeToString(&reply.m_pbData)
    │   │     → 将响应消息序列化回二进制
    │   │
    │   ▼
    └─ conn->sendProtocolData(&reply)
        │
        ├─ TinyPbCodec::encode()
        │   │  构造 TinyPB 帧：START | pkLen | ... | pbData | ... | END
        │   ▼
        └─ 写入 m_outputBuffer
            │
            ▼
TcpConnection::output()
    │  writeHook 协程将 m_outputBuffer 数据写入 socket
    ▼
客户端接收 TCP 字节流
```

---

## 五、Protobuf 提供但项目尚未使用的 API

以下 API 由 Protobuf 框架提供，在更完善的 RPC 框架中可能有用：

### 5.1 Message 反射 API

```cpp
// 通过字段编号/名称动态读写字段（不需要知道具体消息类型）
const google::protobuf::Reflection* refl = msg.GetReflection();
const google::protobuf::FieldDescriptor* field =
    msg.GetDescriptor()->FindFieldByName("id");
int32_t val = refl->GetInt32(msg, field);       // 动态读取
refl->SetInt32(&msg, field, 42);                // 动态设置
```

**用途**：通用日志、调试工具、消息转发代理等需要处理未知消息类型的场景。

### 5.2 JSON 转换

```cpp
#include <google/protobuf/util/json_util.h>

// Protobuf Message ↔ JSON 字符串
std::string json;
google::protobuf::util::MessageToJsonString(msg, &json);

queryNameReq req2;
google::protobuf::util::JsonStringToMessage(json, &req2);
```

**用途**：HTTP 网关、调试接口、跨语言互操作。

### 5.3 Text Format（人类可读格式）

```cpp
#include <google/protobuf/text_format.h>

std::string text;
google::protobuf::TextFormat::PrintToString(msg, &text);
// 输出类似：req_no: 1  id: 42

queryNameReq req2;
google::protobuf::TextFormat::ParseFromString(text, &req2);
```

**用途**：配置文件、调试输出。

### 5.4 Unknown Fields（未知字段处理）

```cpp
// proto3 默认保留未知字段（3.5+ 版本）
const auto& unknown = msg.GetReflection()->GetUnknownFields(msg);
for (int i = 0; i < unknown.field_count(); ++i) {
    // 处理未知字段
}
```

**用途**：前后向兼容性检测、协议升级。

### 5.5 Service Stub（客户端代理）

protoc 在 `cc_generic_services = true` 模式下会同时生成 `Stub` 类：

```cpp
// protoc 自动生成的 Stub 类（在 .pb.h 中）
class QueryService::Stub : public QueryService {
public:
    Stub(google::protobuf::RpcChannel* channel);
    void query_name(RpcController* controller, const queryNameReq* request,
                    queryNameRes* response, Closure* done) override;
};
```

**用途**：RPC 客户端通过 `RpcChannel` 发起远程调用。需要实现 `google::protobuf::RpcChannel` 接口。

### 5.6 RpcChannel（通信通道）

```cpp
class google::protobuf::RpcChannel {
public:
    virtual void CallMethod(
        const MethodDescriptor* method,
        RpcController* controller,
        const Message* request,
        Message* response,
        Closure* done) = 0;
};
```

**用途**：客户端侧的通信抽象。实现此接口可将 Protobuf 的 `Stub::query_name()` 调用映射到网络传输。

---

## 六、项目需要自行完成的工作

Protobuf 只提供了消息序列化和服务定义的基础设施。一个完整的 RPC 框架还需要以下组件，均需自行实现：

### 6.1 已完成 ✓

| 组件 | 文件 | 说明 |
|------|------|------|
| 协议数据结构 | [tinypbdata.h](../mytinyrpc/net/tinypb/tinypbdata.h) | TinyPbStruct 定义帧格式 |
| 编解码器 | [tinypbcodec.h](../mytinyrpc/net/tinypb/tinypbcodec.h) | TinyPB 帧的 encode/decode |
| 分发器 | [tinypbdispatcher.h](../mytinyrpc/net/tinypb/tinypbdispatcher.h) | 服务注册 + 方法路由 |
| 控制器 | [tinypbrpccontroller.h](../mytinyrpc/net/tinypb/tinypbrpccontroller.h) | RpcController 接口实现 |
| 服务端网络层 | [tcpserver.h](../mytinyrpc/net/tcpserver.h), [tcpconnection.h](../mytinyrpc/net/tcpconnection.h) | Reactor + 协程读写 |
| 错误码 | [errorcode.h](../mytinyrpc/comm/errorcode.h) | RPC 错误码常量 |

### 6.2 待完成

#### 6.2.1 RPC 客户端

当前项目只有服务端框架。客户端需要：

- **RpcChannel 实现**：继承 `google::protobuf::RpcChannel`，将 `CallMethod()` 映射为 TinyPB 协议帧的发送与接收。
- **异步调用支持**：使用 `done` Closure 实现异步回调通知。
- **连接管理与复用**：客户端连接池、超时控制、重连策略。
- **Stub 使用示例**：

```cpp
// 目标使用方式（需实现 RpcChannel 后可用）
class TinyPbRpcChannel : public google::protobuf::RpcChannel {
    void CallMethod(
        const MethodDescriptor* method,
        RpcController* controller,
        const Message* request,
        Message* response,
        Closure* done) override
    {
        // 1. 构造 TinyPbStruct
        //    m_serviceFullName = method->full_name()
        //    request->SerializeToString(&m_pbData)
        // 2. 通过 TcpConnection 发送 TinyPB 帧
        // 3. 等待响应帧（同步或异步）
        // 4. 解析响应 TinyPbStruct
        //    response->ParseFromString(reply.m_pbData)
        // 5. 调用 done->Run()（异步时）
    }
};

// 客户端调用
auto channel = std::make_shared<TinyPbRpcChannel>("127.0.0.1:8080");
auto stub = std::make_unique<QueryService::Stub>(channel.get());

queryNameReq req;
req.set_id(42);
queryNameRes res;

// 同步调用
stub->query_name(nullptr, &req, &res, nullptr);

// 异步调用
auto done = google::protobuf::NewCallback([&res]() {
    std::cout << "async result: " << res.name() << std::endl;
});
stub->query_name(nullptr, &req, &res, done);
```

#### 6.2.2 超时控制

- **RpcController 扩展**：添加超时时间设置、超时检测。
- **服务端**：单个 RPC 调用的执行超时限制。
- **客户端**：等待响应的超时限制。

```cpp
// 需要在 TinyPbRpcController 中扩展
class TinyPbRpcController : public google::protobuf::RpcController {
public:
    // 当前已有：SetFailed / Failed / ErrorText
    // 需要新增：
    void SetTimeout(int64_t milliseconds);   // 设置超时时间
    int64_t GetTimeout() const;              // 获取超时时间
    bool IsTimeout() const;                  // 是否超时
};
```

#### 6.2.3 异步 RPC（Closure 回调）

当前 `CallMethod()` 的 `done` 参数传的是 `nullptr`（同步模式）。异步改造需要：

- `dispatch()` 中传入有效的 Closure。
- 服务端在业务方法执行完成后调用 `done->Run()`。
- 结合协程或线程池实现非阻塞处理。

#### 6.2.4 多服务与方法管理

当前支持但不完善：

- **多服务注册**：`m_serviceMap` 已支持多 key，但尚无运行时服务列表查询。
- **方法级中间件**：拦截器/过滤器机制（鉴权、限流、日志）。
- **服务发现**：动态注册/注销服务实例。

#### 6.2.5 错误处理增强

- **错误码体系**：扩展 [errorcode.h](../mytinyrpc/comm/errorcode.h)，定义超时、限流、服务端内部错误等细分错误码。
- **异常捕获**：`CallMethod()` 内业务异常的上报与隔离。
- **错误响应标准化**：统一错误响应格式，客户端可解析。

#### 6.2.6 校验与安全

- **checkNum 真实校验**：[tinypbdata.h](../mytinyrpc/net/tinypb/tinypbdata.h) 中 `m_checkNum` 当前固定为 1，需要实现真正的校验算法（CRC32、Adler32 等）。
- **包长限制**：已有最大包长限制（1MB），可考虑更细粒度的配置。

#### 6.2.7 跨语言与跨协议

- **HTTP 网关**：基于 `MessageToJsonString` / `JsonStringToMessage` 实现 HTTP → RPC 的协议转换。
- **多协议支持**：当前框架已有 `ProtocolType` 枚举（TinyPb / Http），Http 协议的 Codec 尚未实现。

---

## 七、Protobuf API 速查表

### 7.1 头文件索引

| 头文件 | 提供的关键类型 |
|--------|--------------|
| `<google/protobuf/message.h>` | `Message`, `ParseFromString`, `SerializeToString` |
| `<google/protobuf/service.h>` | `Service`, `RpcController`, `RpcChannel`, `Closure` |
| `<google/protobuf/descriptor.h>` | `Descriptor`, `ServiceDescriptor`, `MethodDescriptor`, `FieldDescriptor` |
| `<google/protobuf/util/json_util.h>` | `MessageToJsonString`, `JsonStringToMessage` |
| `<google/protobuf/text_format.h>` | `TextFormat::PrintToString`, `TextFormat::ParseFromString` |
| `<google/protobuf/stubs/common.h>` | `NewCallback`, `NewPermanentCallback` |

### 7.2 核心接口关系

```
google::protobuf::Message
├── ParseFromString()          // 反序列化
├── SerializeToString()        // 序列化
├── GetDescriptor()            // 获取类型元信息
├── New()                      // 动态创建实例
└── [protoc 生成具体的子类如 queryNameReq]

google::protobuf::Service (由 protoc 生成子类)
├── CallMethod()               // 统一调用入口
├── GetDescriptor()            // → ServiceDescriptor
├── GetRequestPrototype()      // → Message 原型
└── GetResponsePrototype()     // → Message 原型

google::protobuf::ServiceDescriptor
├── full_name()                // 服务全名
├── method_count()             // 方法数量
├── method(index)              // 按索引获取
└── FindMethodByName(name)     // 按名称查找 → MethodDescriptor

google::protobuf::MethodDescriptor
├── name()                     // 方法名
├── full_name()                // 全限定名
├── input_type()               // 入参类型 → Descriptor
└── output_type()              // 出参类型 → Descriptor

google::protobuf::RpcController (纯虚接口，需自行实现)
├── Reset()
├── SetFailed() / Failed() / ErrorText()
├── StartCancel() / IsCanceled()
└── NotifyOnCancel()

google::protobuf::RpcChannel (纯虚接口，需自行实现)
└── CallMethod()               // 客户端侧的通信抽象

google::protobuf::Closure (纯虚接口)
└── Run()                      // 回调执行
```
