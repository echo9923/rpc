#include "coroutine/coroutine.h"
#include "net/fdevent.h"

#include <sys/epoll.h>

#include <iostream>

int main()
{
    int readCount = 0;
    int writeCount = 0;

    // 构造 FdEvent，验证 fd 保存
    tinyrpc::FdEvent event(100);
    if (event.getFd() != 100) {
        std::cerr << "[fdevent] FAIL: getFd() != 100" << std::endl;
        return 1;
    }
    if (event.getReactor() != nullptr) {
        std::cerr << "[fdevent] FAIL: default reactor is not nullptr" << std::endl;
        return 1;
    }
    if (event.isRegistered()) {
        std::cerr << "[fdevent] FAIL: default registered state is true" << std::endl;
        return 1;
    }
    if (event.registerToReactor()) {
        std::cerr << "[fdevent] FAIL: register without reactor should fail" << std::endl;
        return 1;
    }

    // 添加 EPOLLIN，验证包含 EPOLLIN
    event.addListenEvent(EPOLLIN);
    if (!(event.getListenEvents() & EPOLLIN)) {
        std::cerr << "[fdevent] FAIL: EPOLLIN not set after addListenEvent" << std::endl;
        return 1;
    }

    // 添加 EPOLLOUT，验证同时包含 EPOLLIN 和 EPOLLOUT
    event.addListenEvent(EPOLLOUT);
    if ((event.getListenEvents() & (EPOLLIN | EPOLLOUT)) != (EPOLLIN | EPOLLOUT)) {
        std::cerr << "[fdevent] FAIL: both EPOLLIN|EPOLLOUT expected after add" << std::endl;
        return 1;
    }

    // 删除 EPOLLOUT，验证不再包含 EPOLLOUT
    event.delListenEvent(EPOLLOUT);
    if (event.getListenEvents() & EPOLLOUT) {
        std::cerr << "[fdevent] FAIL: EPOLLOUT still set after delListenEvent" << std::endl;
        return 1;
    }
    if (!(event.getListenEvents() & EPOLLIN)) {
        std::cerr << "[fdevent] FAIL: EPOLLIN was cleared unexpectedly" << std::endl;
        return 1;
    }

    // 注册读回调
    event.setReadCallback([&readCount]() {
        ++readCount;
    });

    // 注册写回调
    event.setWriteCallback([&writeCount]() {
        ++writeCount;
    });

    // 触发 EPOLLIN，验证只触发读回调
    event.handleEvent(EPOLLIN);
    if (readCount != 1) {
        std::cerr << "[fdevent] FAIL: readCount != 1 after handleEvent(EPOLLIN)" << std::endl;
        return 1;
    }
    if (writeCount != 0) {
        std::cerr << "[fdevent] FAIL: writeCount != 0 after handleEvent(EPOLLIN)" << std::endl;
        return 1;
    }

    // 触发 EPOLLOUT，验证只触发写回调
    event.handleEvent(EPOLLOUT);
    if (readCount != 1) {
        std::cerr << "[fdevent] FAIL: readCount changed after handleEvent(EPOLLOUT)" << std::endl;
        return 1;
    }
    if (writeCount != 1) {
        std::cerr << "[fdevent] FAIL: writeCount != 1 after handleEvent(EPOLLOUT)" << std::endl;
        return 1;
    }

    // 同时触发 EPOLLIN | EPOLLOUT，验证两个回调都触发
    event.handleEvent(EPOLLIN | EPOLLOUT);
    if (readCount != 2) {
        std::cerr << "[fdevent] FAIL: readCount != 2 after handleEvent(EPOLLIN|EPOLLOUT)" << std::endl;
        return 1;
    }
    if (writeCount != 2) {
        std::cerr << "[fdevent] FAIL: writeCount != 2 after handleEvent(EPOLLIN|EPOLLOUT)" << std::endl;
        return 1;
    }

    // ────────────────────────────────────────────
    // 协程挂载点测试（任务二十）
    // ────────────────────────────────────────────

    // 创建一个 FdEvent 和一个 Coroutine（仅验证指针操作，不 resume）
    tinyrpc::FdEvent evt(-1);
    tinyrpc::Coroutine coro([]() {
        tinyrpc::Coroutine::yield();
    });

    // 默认 m_coroutine 应为 nullptr
    if (evt.getCoroutine() != nullptr) {
        std::cerr << "[fdevent] FAIL: default getCoroutine() should be nullptr" << std::endl;
        return 1;
    }

    // setCoroutine 后应能取回同一指针
    evt.setCoroutine(&coro);
    if (evt.getCoroutine() != &coro) {
        std::cerr << "[fdevent] FAIL: getCoroutine() should return set pointer" << std::endl;
        return 1;
    }

    // clearCoroutine 后应为 nullptr
    evt.clearCoroutine();
    if (evt.getCoroutine() != nullptr) {
        std::cerr << "[fdevent] FAIL: getCoroutine() should be nullptr after clear" << std::endl;
        return 1;
    }

    // ────────────────────────────────────────────
    // 全部通过
    // ────────────────────────────────────────────
    std::cout << "[fdevent] PASS" << std::endl;
    return 0;
}
