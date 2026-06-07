#pragma once

#include "net/fdevent.h"

#include <memory>
#include <unordered_map>

namespace tinyrpc {

class Reactor;

// FdEventContainer — 线程级 fd → FdEvent 查找容器。
//
// 为透明 hook (extern "C" read/write/accept/connect) 提供 O(1) 的
// fd → FdEvent 查找。每个 IOThread 拥有独立的 thread-local 实例。
//
// 两种注册方式：
//   - registerFdEvent()：索引外部拥有的 FdEvent*，容器不拥有其生命周期。
//   - getOrCreate()：惰性创建 FdEvent，由容器 unique_ptr 拥有。
//
// remove() 无条件移除条目（无论来源），close fd 前调用防止悬空。
class FdEventContainer {
 public:
    // 获取当前线程的 FdEventContainer 单例
    static FdEventContainer& getInstance();

    // 索引外部拥有的 FdEvent。容器不拥有其生命周期。
    // 同一 fd 已存在时不覆盖，返回 false。
    bool registerFdEvent(FdEvent *event);

    // 按 fd 查找或创建 FdEvent。
    // 找到返回已有指针；未找到则创建新 FdEvent 并绑定当前线程 Reactor。
    // 同一 fd 多次调用保证返回同一指针（地址稳定）。
    FdEvent* getOrCreate(int fd);

    // 仅查找，不创建。未找到返回 nullptr。
    FdEvent* get(int fd) const;

    // 无条件移除 fd 条目（无论外部索引还是容器自有）。
    void remove(int fd);

 private:
    FdEventContainer() = default;
    FdEventContainer(const FdEventContainer&) = delete;
    FdEventContainer& operator=(const FdEventContainer&) = delete;

    struct Entry {
        FdEvent *ptr {nullptr};            // 指向 FdEvent 对象（始终非空）
        std::unique_ptr<FdEvent> owned;    // 非空表示容器拥有该 FdEvent
    };

    std::unordered_map<int, Entry> m_events;
};

}  // namespace tinyrpc
