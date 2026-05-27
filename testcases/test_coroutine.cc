/*
 * test_coroutine.cc — 任务二十：最小协程对象验收测试。
 *
 * 测试覆盖：
 *   1. 主协程检测（IsMainCoroutine）
 *   2. 协程创建、resume、Yield、状态切换（Ready → Running → Suspended）
 *   3. 再次 resume 恢复执行直到 Finished
 *   4. 对已结束协程再次 resume 不会重复执行
 *   5. 协程 ID 分配（主协程为 0，子协程从 1 开始）
 */

#include "coroutine/coroutine.h"

#include <cstdlib>
#include <iostream>
#include <string>
#include <vector>

int main()
{
    // ────────────────────────────────────────────
    // Test 1：程序开始时应在主协程中
    // ────────────────────────────────────────────
    if (!tinyrpc::Coroutine::IsMainCoroutine()) {
        std::cerr << "[coroutine] FAIL: IsMainCoroutine() should be true "
                     "at program start"
                  << std::endl;
        return 1;
    }

    if (tinyrpc::Coroutine::GetCurrentCoroutine()
        != tinyrpc::Coroutine::GetMainCoroutine()) {
        std::cerr << "[coroutine] FAIL: GetCurrentCoroutine() should equal "
                     "GetMainCoroutine() at program start"
                  << std::endl;
        return 1;
    }

    if (tinyrpc::Coroutine::GetMainCoroutine()->getId() != 0) {
        std::cerr << "[coroutine] FAIL: main coroutine id should be 0"
                  << std::endl;
        return 1;
    }

    // ────────────────────────────────────────────
    // Test 2：协程创建、resume、Yield、状态验证
    // ────────────────────────────────────────────
    std::vector<std::string> trace;
    int execCount = 0;

    tinyrpc::Coroutine co([&trace, &execCount]() {
        ++execCount;

        trace.push_back("start");

        // 在协程内部确认 IsMainCoroutine() == false
        if (tinyrpc::Coroutine::IsMainCoroutine()) {
            std::cerr << "[coroutine] FAIL: IsMainCoroutine() should be "
                         "false inside coroutine"
                      << std::endl;
            // 不能直接 exit(1)，因为可能在协程内调用 exit 不安全。
            // 用抛异常或标记错误替代。此处使用全局标记，但由于 lambda
            // 捕获有限，先记录错误字符串以便主流程判断。
            trace.push_back("ERROR_IS_MAIN");
        }

        // 让出执行权，回到主协程
        tinyrpc::Coroutine::Yield();

        trace.push_back("end");
    });

    // 第一次 resume：协程应执行到 Yield() 并返回
    co.resume();

    if (execCount != 1) {
        std::cerr << "[coroutine] FAIL: execCount != 1 after first resume"
                  << std::endl;
        return 1;
    }

    // 验证协程状态为 Suspended
    if (co.getState() != tinyrpc::CoroutineState::Suspended) {
        std::cerr << "[coroutine] FAIL: state should be Suspended "
                     "after Yield"
                  << std::endl;
        return 1;
    }

    // 验证执行痕迹：只有 "start"
    if (trace.size() != 1 || trace[0] != "start") {
        std::cerr << "[coroutine] FAIL: trace should contain only 'start' "
                     "after first resume"
                  << std::endl;
        return 1;
    }

    // ────────────────────────────────────────────
    // Test 3：第二次 resume，协程继续执行到结束
    // ────────────────────────────────────────────
    co.resume();

    if (execCount != 1) {
        std::cerr << "[coroutine] FAIL: execCount changed unexpectedly "
                     "after second resume"
                  << std::endl;
        return 1;
    }

    // 验证协程状态为 Finished
    if (!co.isFinished()) {
        std::cerr << "[coroutine] FAIL: state should be Finished "
                     "after coroutine completes"
                  << std::endl;
        return 1;
    }

    if (co.getState() != tinyrpc::CoroutineState::Finished) {
        std::cerr << "[coroutine] FAIL: getState() should return Finished"
                  << std::endl;
        return 1;
    }

    // 验证执行痕迹包含 "start" 和 "end"
    if (trace.size() != 2 || trace[0] != "start" || trace[1] != "end") {
        std::cerr << "[coroutine] FAIL: trace should contain 'start' and 'end'"
                  << std::endl;
        return 1;
    }

    // ────────────────────────────────────────────
    // Test 4：对已结束协程再次 resume，不应重复执行
    // ────────────────────────────────────────────
    co.resume();

    // execCount 应仍为 1
    if (execCount != 1) {
        std::cerr << "[coroutine] FAIL: coroutine callback was executed "
                     "again after Finished"
                  << std::endl;
        return 1;
    }

    // 验证协程状态仍为 Finished
    if (co.getState() != tinyrpc::CoroutineState::Finished) {
        std::cerr << "[coroutine] FAIL: state should remain Finished "
                     "after extra resume"
                  << std::endl;
        return 1;
    }

    // ────────────────────────────────────────────
    // Test 5：协程 ID 分配
    // ────────────────────────────────────────────
    // 主协程 ID 为 0
    if (tinyrpc::Coroutine::GetMainCoroutine()->getId() != 0) {
        std::cerr << "[coroutine] FAIL: main coroutine id should be 0"
                  << std::endl;
        return 1;
    }

    // 子协程 ID 不为 0
    if (co.getId() == 0) {
        std::cerr << "[coroutine] FAIL: sub coroutine id should not be 0"
                  << std::endl;
        return 1;
    }

    // 多个协程 ID 互不相同
    tinyrpc::Coroutine co2([]() {
        tinyrpc::Coroutine::Yield();
    });
    tinyrpc::Coroutine co3([]() {
        tinyrpc::Coroutine::Yield();
    });

    if (co2.getId() == co.getId()) {
        std::cerr << "[coroutine] FAIL: co2 should have unique id"
                  << std::endl;
        return 1;
    }

    if (co3.getId() == co.getId() || co3.getId() == co2.getId()) {
        std::cerr << "[coroutine] FAIL: co3 should have unique id"
                  << std::endl;
        return 1;
    }

    // ────────────────────────────────────────────
    // 全部通过
    // ────────────────────────────────────────────
    std::cout << "[coroutine] PASS" << std::endl;
    return 0;
}
