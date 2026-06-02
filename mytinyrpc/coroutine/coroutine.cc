#include "coroutine/coroutine.h"

#include <cassert>
#include <cstdint>
#include <cstdlib>
#include <cstring>

namespace tinyrpc {

// ─────────────────────────────────────────────
// 线程局部变量
// ─────────────────────────────────────────────

// 主协程指针：每个 IO 线程有且仅有一个主协程。
// 首次通过 GetMainCoroutine() 或创建第一个子协程时懒初始化。
thread_local Coroutine* Coroutine::t_mainCoroutine = nullptr;

// 当前正在执行的协程指针。
// 初始为 nullptr，懒初始化时指向主协程。
thread_local Coroutine* Coroutine::t_curCoroutine = nullptr;

// 全局协程 ID 计数器，从 1 开始分配（主协程 ID 固定为 0）。
static int s_nextCorId = 1;

// ─────────────────────────────────────────────
// 协程入口函数（通过 coctx_swap 跳转至此）
// ─────────────────────────────────────────────

void Coroutine::CoFunc(Coroutine* co)
{
    // 执行用户回调。
    // 回调内部可能多次调用 Coroutine::Yield() 让出执行权，
    // 每次 resume() 后会从 Yield() 返回继续执行。
    if (co->m_callback) {
        co->m_callback();
    }

    // 回调执行完毕，标记为 Finished。
    co->m_state = CoroutineState::Finished;

    // 切回主协程。由于状态已经是 Finished，Yield 内部不会
    // 将其覆盖为 Suspended。
    Coroutine::Yield();

    // 注意：此处之后不会再被执行，因为 Finished 状态导致
    // resume() 不会再次进入此协程。
}

// ─────────────────────────────────────────────
// 构造 / 析构
// ─────────────────────────────────────────────

// 私有构造：仅用于创建主协程。
// 主协程没有独立栈，没有回调，ID 固定为 0。
Coroutine::Coroutine()
    : m_corId(0)
{
}

// 公有构造：创建子协程。
// @param cb        协程入口回调。
// @param stackSize 独立栈大小（字节），默认 128KB。
Coroutine::Coroutine(std::function<void()> cb, size_t stackSize)
    : m_corId(s_nextCorId++),
      m_stackSize(stackSize),
      m_callback(std::move(cb))
{
    // 懒初始化主协程。如果当前线程还没有主协程，先创建一个。
    if (t_mainCoroutine == nullptr) {
        t_mainCoroutine = new Coroutine();
        t_curCoroutine = t_mainCoroutine;
    }

    // 分配独立栈空间。
    m_stackSp = static_cast<char*>(std::malloc(m_stackSize));
    assert(m_stackSp != nullptr && "Coroutine: malloc stack failed");

    initContext();
}

Coroutine::~Coroutine()
{
    // 释放独立栈空间。
    if (m_stackSp != nullptr) {
        std::free(m_stackSp);
        m_stackSp = nullptr;
    }
}

// ─────────────────────────────────────────────
// resume — 从主协程恢复子协程执行
// ─────────────────────────────────────────────

void Coroutine::resume()
{
    // 只能从主协程恢复子协程。
    if (t_curCoroutine != t_mainCoroutine) {
        return;
    }

    // 已完成的协程不允许重复执行。
    if (m_state == CoroutineState::Finished) {
        return;
    }

    // 更新状态和当前协程指针。
    m_state = CoroutineState::Running;
    t_curCoroutine = this;

    // 切换上下文：保存主协程寄存器，加载当前协程寄存器。
    // 当协程调用 Yield() 后会切回此处继续执行。
    coctx_swap(&(t_mainCoroutine->m_coctx), &(m_coctx));
}

// ─────────────────────────────────────────────
// Yield — 从子协程让出执行权回到主协程
// ─────────────────────────────────────────────

void Coroutine::Yield()
{
    // 主协程不允许让出。
    if (t_curCoroutine == t_mainCoroutine || t_mainCoroutine == nullptr) {
        return;
    }

    Coroutine* co = t_curCoroutine;

    // 如果协程还未结束，标记为 Suspended。
    // 如果协程已经 Finished（CoFunc 执行完毕），保持 Finished 不变。
    if (co->m_state != CoroutineState::Finished) {
        co->m_state = CoroutineState::Suspended;
    }

    // 切换当前协程指针回主协程。
    t_curCoroutine = t_mainCoroutine;

    // 切换上下文：保存当前协程寄存器，恢复主协程寄存器。
    // 当下次 resume() 被调用时，会从 coctx_swap 处继续执行。
    coctx_swap(&(co->m_coctx), &(t_mainCoroutine->m_coctx));
}

bool Coroutine::reset(std::function<void()> cb)
{
    // Running/Suspended 状态仍保存着有效执行现场，不能覆盖上下文。
    if (m_state == CoroutineState::Running || m_state == CoroutineState::Suspended) {
        return false;
    }

    m_callback = std::move(cb);
    initContext();
    return true;
}

void Coroutine::initContext()
{
    // 计算栈顶指针（栈从高地址向低地址增长）。
    char* top = m_stackSp + m_stackSize;

    // 16 字节对齐：System V AMD64 ABI 要求在 call 指令之前
    // 栈必须 16 字节对齐。coctx_swap 返回时用 ret 跳转，
    // ret 会弹出 8 字节，因此栈顶在 ret 前应满足 (rsp % 16) == 8。
    // 这里统一把 top 向下对齐到 16 字节边界即可。
    top = reinterpret_cast<char*>(
        (reinterpret_cast<uintptr_t>(top)) & ~static_cast<uintptr_t>(15)
    );

    // 清空 coctx，设置寄存器上下文。
    std::memset(&m_coctx, 0, sizeof(m_coctx));

    // kRSP = 栈顶：coctx_swap 恢复时直接 movq 到 rsp。
    m_coctx.regs[kRSP] = top;

    // kRBP = 栈基址：与 kRSP 相同，简化栈帧布局。
    m_coctx.regs[kRBP] = top;

    // kRETAddr = CoFunc 入口地址：coctx_swap 通过 ret 跳转到此。
    // 注意：在 coctx_swap.S 的实现中，ret 从栈顶弹出地址后跳转，
    // 最终会执行 CoFunc(this)。
    m_coctx.regs[kRETAddr] = reinterpret_cast<void*>(CoFunc);

    // kRDI = this 指针：System V AMD64 ABI 中 rdi 传递第一个参数。
    // CoFunc 的参数为 Coroutine*，所以 this 作为第一个参数传入。
    m_coctx.regs[kRDI] = reinterpret_cast<void*>(this);

    // 状态初始为 Ready，等待首次 resume()。
    m_state = CoroutineState::Ready;
}

// ─────────────────────────────────────────────
// 静态工具方法
// ─────────────────────────────────────────────

Coroutine* Coroutine::GetCurrentCoroutine()
{
    if (t_curCoroutine == nullptr) {
        t_mainCoroutine = new Coroutine();
        t_curCoroutine = t_mainCoroutine;
    }
    return t_curCoroutine;
}

Coroutine* Coroutine::GetMainCoroutine()
{
    if (t_mainCoroutine == nullptr) {
        t_mainCoroutine = new Coroutine();
        t_curCoroutine = t_mainCoroutine;
    }
    return t_mainCoroutine;
}

bool Coroutine::IsMainCoroutine()
{
    if (t_mainCoroutine == nullptr || t_curCoroutine == t_mainCoroutine) {
        return true;
    }
    return false;
}

}  // namespace tinyrpc
