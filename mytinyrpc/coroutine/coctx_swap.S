# coctx_swap.S — x86-64 协程上下文切换汇编实现。
#
# 复制自 Tencent/libco (https://github.com/Tencent/libco/blob/master/coctx_swap.S)。
#
# 调用约定 (System V AMD64 ABI)：
#   %rdi = old_ctx (coctx*)，当前寄存器状态保存目标
#   %rsi = new_ctx (coctx*)，新协程寄存器状态来源
#
# coctx::regs[] 数组元素大小为 8 字节（指针/寄存器值），
# 索引 * 8 即为内存偏移。例如 regs[13] (kRSP) 的偏移为 13 * 8 = 104。
#
# 保存/恢复 14 个通用寄存器：r15, r14, r13, r12, r9, r8,
#                               rbp, rdi, rsi, retaddr, rdx, rcx, rbx, rsp。

.globl coctx_swap

coctx_swap:
    # ─────────────────────────────────────────────
    # 阶段一：保存当前协程的寄存器到 old_ctx (%rdi)
    # ─────────────────────────────────────────────

    # kRSP = 13 → 偏移 104
    # %rsp: 栈顶指针寄存器（Stack Pointer），指向当前栈的顶部
    # %rax: 通用寄存器（Accumulator），用作临时存储
    leaq    (%rsp), %rax          # leaq: 加载有效地址。把 %rsp（栈顶指针）的值复制到 %rax
                                  # %rax = %rsp（不改变 %rsp，只是复制一份）
    movq    %rax, 104(%rdi)       # movq: 移动四字（8字节）。把 %rax 的值存到 %rdi+104 的内存位置
                                  # %rdi: old_ctx 指针（第一个参数）
                                  # 即 old_ctx->regs[13] = 当前栈顶指针

    # %rbx: 被调用者保存寄存器（callee-saved），函数调用后需恢复原值
    movq    %rbx, 96(%rdi)        # 保存 %rbx 到 old_ctx->regs[12]
                                  # movq: 源操作数 %rbx（寄存器），目标操作数 96(%rdi)（内存）
    # %rcx: 调用者保存寄存器（caller-saved），第四个参数寄存器
    movq    %rcx, 88(%rdi)        # 保存 %rcx 到 old_ctx->regs[11]
    # %rdx: 调用者保存寄存器（caller-saved），第三个参数寄存器
    movq    %rdx, 80(%rdi)        # 保存 %rdx 到 old_ctx->regs[10]

    # 保存返回地址：当前栈顶保存了 coctx_swap 返回后要执行的地址。
    # retaddr = [rsp]；我们已经把 rsp 存到 rax，所以：
    # rax 中存的是旧的 rsp 值，[rax] 就是返回地址。
    movq    0(%rax), %rax         # 从 %rax 指向的内存位置（即旧栈顶）读取 8 字节到 %rax
                                  # 这 8 字节就是调用 coctx_swap 时压入的返回地址
                                  # movq: 从内存 0(%rax) 读取到寄存器 %rax
    movq    %rax, 72(%rdi)        # 把返回地址保存到 old_ctx->regs[9]

    # %rsi: 调用者保存寄存器（caller-saved），第二个参数寄存器（new_ctx 指针）
    movq    %rsi, 64(%rdi)        # 保存 %rsi（new_ctx 指针）到 old_ctx->regs[8]
    # %rdi: 调用者保存寄存器（caller-saved），第一个参数寄存器（old_ctx 指针）
    movq    %rdi, 56(%rdi)        # 保存 %rdi（old_ctx 指针）到 old_ctx->regs[7]
    # %rbp: 栈基址寄存器（Base Pointer），指向当前函数栈帧的底部
    movq    %rbp, 48(%rdi)        # 保存 %rbp（栈基址）到 old_ctx->regs[6]
    # %r8: 调用者保存寄存器（caller-saved），第五个参数寄存器
    movq    %r8,  40(%rdi)        # 保存 %r8 到 old_ctx->regs[5]
    # %r9: 调用者保存寄存器（caller-saved），第六个参数寄存器
    movq    %r9,  32(%rdi)        # 保存 %r9 到 old_ctx->regs[4]
    # %r12: 被调用者保存寄存器（callee-saved）
    movq    %r12, 24(%rdi)        # 保存 %r12 到 old_ctx->regs[3]
    # %r13: 被调用者保存寄存器（callee-saved）
    movq    %r13, 16(%rdi)        # 保存 %r13 到 old_ctx->regs[2]
    # %r14: 被调用者保存寄存器（callee-saved）
    movq    %r14, 8(%rdi)         # 保存 %r14 到 old_ctx->regs[1]
    # %r15: 被调用者保存寄存器（callee-saved）
    movq    %r15, (%rdi)          # 保存 %r15 到 old_ctx->regs[0]
                                  # (%rdi) 等价于 0(%rdi)，即 %rdi+0 的位置

    xorq    %rax, %rax            # xorq: 异或运算。任何数与自己异或结果为 0
                                  # 清空 %rax 寄存器（作为返回值，coctx_swap 无返回值）

    # ─────────────────────────────────────────────
    # 阶段二：恢复新协程的寄存器从 new_ctx (%rsi)
    # ─────────────────────────────────────────────

    # %rsi: new_ctx 指针（第二个参数）
    movq    48(%rsi), %rbp        # 从 new_ctx->regs[6] 恢复 %rbp（栈基址）
                                  # movq: 从内存 48(%rsi) 读取到寄存器 %rbp
    movq    104(%rsi), %rsp       # 从 new_ctx->regs[13] 恢复 %rsp（栈顶指针）
                                  # ⚠️ 最关键的一行：从此刻起，CPU 的栈切换到新协程的栈

    movq    (%rsi), %r15          # 从 new_ctx->regs[0] 恢复 %r15（被调用者保存寄存器）
    movq    8(%rsi), %r14         # 从 new_ctx->regs[1] 恢复 %r14（被调用者保存寄存器）
    movq    16(%rsi), %r13        # 从 new_ctx->regs[2] 恢复 %r13（被调用者保存寄存器）
    movq    24(%rsi), %r12        # 从 new_ctx->regs[3] 恢复 %r12（被调用者保存寄存器）
    movq    32(%rsi), %r9         # 从 new_ctx->regs[4] 恢复 %r9（调用者保存寄存器，第六个参数）
    movq    40(%rsi), %r8         # 从 new_ctx->regs[5] 恢复 %r8（调用者保存寄存器，第五个参数）
    movq    56(%rsi), %rdi        # 从 new_ctx->regs[7] 恢复 %rdi（调用者保存寄存器，第一个参数）
                                  # 恢复后的 %rdi 就是 this 指针（Coroutine*）
    movq    80(%rsi), %rdx        # 从 new_ctx->regs[10] 恢复 %rdx（调用者保存寄存器，第三个参数）
    movq    88(%rsi), %rcx        # 从 new_ctx->regs[11] 恢复 %rcx（调用者保存寄存器，第四个参数）
    movq    96(%rsi), %rbx        # 从 new_ctx->regs[12] 恢复 %rbx（被调用者保存寄存器）

    # 准备返回地址：在恢复后的栈上压入返回地址，
    # 然后执行 ret 跳转到新协程的 kRETAddr。
    # 当前 rsp 已指向新栈，将新栈上的返回地址弹出并不使用，
    # 然后将 kRETAddr 压入栈顶，ret 即可跳转。
    leaq    8(%rsp), %rsp         # leaq: 计算新地址 %rsp+8，并更新 %rsp
                                  # 相当于 %rsp += 8，跳过新栈顶的 8 字节
                                  # 这是为了模拟 ret 指令会弹出 8 字节的行为
    pushq   72(%rsi)              # pushq: 压栈。把 new_ctx->regs[9]（返回地址）压入栈顶
                                  # pushq 会先 %rsp -= 8，然后把值写入 [%rsp]
                                  # 压入后，栈顶 8 字节就是目标跳转地址

    movq    64(%rsi), %rsi        # 从 new_ctx->regs[8] 恢复 %rsi（调用者保存寄存器，第二个参数）
                                  # 必须在 ret 之前恢复，因为 %rsi 是调用约定参数寄存器

    ret                           # ret: 返回指令。从栈顶弹出 8 字节，跳转到该地址
                                  # 弹出的正是刚才 pushq 压入的返回地址
                                  # CPU 从此开始执行新协程的代码
