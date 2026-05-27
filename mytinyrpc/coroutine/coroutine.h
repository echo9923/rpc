#pragma once

#include "coroutine/coctx.h"

#include <cstddef>
#include <functional>

namespace tinyrpc {

// CoroutineState — 协程运行状态枚举。
//
// 状态转换图：
//   Ready ──resume()──▶ Running ──Yield()──▶ Suspended
//                          │                    │
//                          ▼                    │
//                       Finished ◀──────────────┘
//                          │
//                          ▼ (resume() 无操作)
//                       Finished (不变)
enum class CoroutineState {
    Ready,      // 已创建，等待首次 resume
    Running,    // 正在执行回调中
    Suspended,  // 已让出执行权，等待再次 resume
    Finished,   // 回调已执行完毕
};

// Coroutine — 最小协程对象。
//
// 每个 Coroutine 拥有独立栈空间（构造时 malloc，析构时 free）。
// 使用 coctx_swap 汇编原语在主协程与子协程之间切换执行上下文。
//
// 用法示例：
//   Coroutine co([]() {
//       // 协程逻辑
//       Coroutine::Yield();  // 让出执行权
//       // 继续执行
//   });
//   co.resume();  // 从主协程切到子协程
//
// 线程安全性：线程局部存储（thread_local），每个线程有独立的主协程。
// 当前实现仅支持单线程内协程切换，不支持跨线程迁移协程。
class Coroutine {
 public:
    // 构造一个子协程。
    // @param cb        协程入口回调函数。
    // @param stackSize 协程独立栈大小（字节），默认 128KB。
    explicit Coroutine(std::function<void()> cb, size_t stackSize = 128 * 1024);

    // 析构函数：释放独立栈空间。
    ~Coroutine();

    // 恢复此协程执行。
    // 只能在主协程中调用。如果协程已经 Finished，则立即返回。
    // 内部通过 coctx_swap 从主协程切换到当前协程。
    void resume();

    // 让出当前协程的执行权，切回主协程。
    // 只能在非主协程中调用。
    static void Yield();

    // 获取当前正在执行的协程指针。
    static Coroutine* GetCurrentCoroutine();

    // 获取主协程指针。
    static Coroutine* GetMainCoroutine();

    // 判断当前是否在主协程中执行。
    static bool IsMainCoroutine();

    // 获取当前协程状态。
    CoroutineState getState() const { return m_state; }

    // 判断协程是否已执行完毕。
    bool isFinished() const { return m_state == CoroutineState::Finished; }

    // 获取协程唯一 ID。
    // 主协程 ID 为 0，子协程 ID 从 1 开始递增。
    int getId() const { return m_corId; }

 private:
    // 私有无参构造：仅用于创建主协程（无栈、无回调、ID=0）。
    Coroutine();

    // 协程入口包装函数。
    // 通过 coctx_swap 跳转到此函数后，执行 m_callback，
    // 完成后设置 Finished 状态并切回主协程。
    static void CoFunc(Coroutine* co);

    int m_corId {0};                    // 协程 ID，主协程为 0
    coctx m_coctx {};                   // 寄存器上下文
    size_t m_stackSize {0};             // 独立栈大小
    char* m_stackSp {nullptr};          // 独立栈起始地址（malloc 分配）
    CoroutineState m_state {CoroutineState::Ready};
    std::function<void()> m_callback;   // 协程入口回调

    // 线程局部存储：每个线程独立的主协程和当前协程指针。
    static thread_local Coroutine* t_mainCoroutine;
    static thread_local Coroutine* t_curCoroutine;
};

}  // namespace tinyrpc
