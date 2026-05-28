#pragma once

#include <memory>

namespace tinyrpc {

// 协议类型枚举，标识当前使用的是哪种上层协议。
// 后续新增协议时在此处添加枚举值即可。
enum class ProtocolType {
    TinyPb = 1,
    Http = 2,
};

// AbstractData 是所有协议数据对象的抽象基类。
// 具体协议（如 HttpRequest、TinyPbStruct）继承此类，
// 添加协议特有的字段，同时复用 m_encodeSucc / m_decodeSucc 状态标记。
class AbstractData {
 public:
    using Ptr = std::shared_ptr<AbstractData>;

    virtual ~AbstractData() = default;

    // encode 成功后由 Codec 设置为 true，默认 false
    bool m_encodeSucc {false};

    // decode 成功后由 Codec 设置为 true，默认 false
    bool m_decodeSucc {false};
};

}
