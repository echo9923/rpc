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
    # 保存当前协程的寄存器到 old_ctx (%rdi)
    # ─────────────────────────────────────────────

    # kRSP = 13 → 偏移 104
    leaq    (%rsp), %rax          # rax = 当前 rsp（栈顶）
    movq    %rax, 104(%rdi)       # old_ctx->regs[13] = rsp

    movq    %rbx, 96(%rdi)        # old_ctx->regs[12] = rbx
    movq    %rcx, 88(%rdi)        # old_ctx->regs[11] = rcx
    movq    %rdx, 80(%rdi)        # old_ctx->regs[10] = rdx

    # 保存返回地址：当前栈顶保存了 coctx_swap 返回后要执行的地址。
    # retaddr = [rsp]；我们已经把 rsp 存到 rax，所以：
    # rax 中存的是旧的 rsp 值，[rax] 就是返回地址。
    movq    0(%rax), %rax         # rax = [旧 rsp] = 返回地址
    movq    %rax, 72(%rdi)        # old_ctx->regs[9] = 返回地址

    movq    %rsi, 64(%rdi)        # old_ctx->regs[8] = rsi
    movq    %rdi, 56(%rdi)        # old_ctx->regs[7] = rdi
    movq    %rbp, 48(%rdi)        # old_ctx->regs[6] = rbp
    movq    %r8,  40(%rdi)        # old_ctx->regs[5] = r8
    movq    %r9,  32(%rdi)        # old_ctx->regs[4] = r9
    movq    %r12, 24(%rdi)        # old_ctx->regs[3] = r12
    movq    %r13, 16(%rdi)        # old_ctx->regs[2] = r13
    movq    %r14, 8(%rdi)         # old_ctx->regs[1] = r14
    movq    %r15, (%rdi)          # old_ctx->regs[0] = r15

    xorq    %rax, %rax            # rax = 0（清空返回值）

    # ─────────────────────────────────────────────
    # 恢复新协程的寄存器从 new_ctx (%rsi)
    # ─────────────────────────────────────────────

    movq    48(%rsi), %rbp        # rbp = new_ctx->regs[6]
    movq    104(%rsi), %rsp       # rsp = new_ctx->regs[13]  ← 切换栈

    movq    (%rsi), %r15          # r15 = new_ctx->regs[0]
    movq    8(%rsi), %r14         # r14 = new_ctx->regs[1]
    movq    16(%rsi), %r13        # r13 = new_ctx->regs[2]
    movq    24(%rsi), %r12        # r12 = new_ctx->regs[3]
    movq    32(%rsi), %r9         # r9  = new_ctx->regs[4]
    movq    40(%rsi), %r8         # r8  = new_ctx->regs[5]
    movq    56(%rsi), %rdi        # rdi = new_ctx->regs[7]  (this 指针)
    movq    80(%rsi), %rdx        # rdx = new_ctx->regs[10]
    movq    88(%rsi), %rcx        # rcx = new_ctx->regs[11]
    movq    96(%rsi), %rbx        # rbx = new_ctx->regs[12]

    # 准备返回地址：在恢复后的栈上压入返回地址，
    # 然后执行 ret 跳转到新协程的 kRETAddr。
    # 当前 rsp 已指向新栈，将新栈上的返回地址弹出并不使用，
    # 然后将 kRETAddr 压入栈顶，ret 即可跳转。
    leaq    8(%rsp), %rsp         # 跳过新栈顶的 8 字节（模拟 ret 消耗）
    pushq   72(%rsi)              # 压入 new_ctx->regs[9] = 返回地址

    movq    64(%rsi), %rsi        # rsi = new_ctx->regs[8]

    ret                           # 弹出栈顶地址并跳转
