#pragma once

/*
 * coctx.h — x86-64 协程上下文切换寄存器偏移与结构体定义。
 *
 * 寄存器数量：14 个通用寄存器。
 * 偏移常量命名参考 libco (Tencent) 的 coctx.h，便于对照调试。
 *
 * 此文件不感知栈布局，只定义寄存器在 Coctx::m_regs[] 数组中的位置，
 * 偏移值由 coctxswap.s 的保存/恢复顺序决定，两者必须同步。
 */

namespace tinyrpc {

enum {
    kRBP = 6,      // rbp — 栈基址
    kRDI = 7,      // rdi — 函数调用第一个参数（用于传递 Coroutine*）
    kRSI = 8,      // rsi — 函数调用第二个参数
    kRETAddr = 9,  // 返回地址，coctxSwap 会跳转到此处继续执行
    kRSP = 13,     // rsp — 栈顶指针
};

// coctx 保存 14 个通用寄存器的值，用于协程上下文切换。
// 顺序必须与 coctxswap.s 中的保存/恢复顺序严格一致。
struct Coctx {
    void* m_regs[14];
};

}  // namespace tinyrpc

extern "C" {

/*
 * coctxSwap — 协程上下文切换。
 *
 * @param[in,out] old_ctx  保存当前 CPU 寄存器状态的 Coctx 指针。
 * @param[in]     new_ctx  加载新协程寄存器状态的 Coctx 指针。
 *
 * 调用后：当前协程的寄存器被保存到 old_ctx，CPU 寄存器从 new_ctx 恢复，
 * 程序将从 new_ctx.kRETAddr 处继续执行。
 *
 * 注意：此函数不会返回两次，而是在 old_ctx 对应的协程被再次恢复时返回。
 */
extern void coctxSwap(tinyrpc::Coctx* old_ctx, tinyrpc::Coctx* new_ctx);

}  // extern "C"
