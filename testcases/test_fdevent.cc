#include "coroutine/coroutine.h"
#include "net/fdevent.h"
#include "net/fdeventcontainer.h"
#include "net/reactor.h"

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
    // FdEventContainer 测试（任务九十二）
    // ────────────────────────────────────────────

    // 1) 同一 fd 多次 getOrCreate 返回同一对象
    {
        auto& container = tinyrpc::FdEventContainer::getInstance();
        tinyrpc::FdEvent *e1 = container.getOrCreate(42);
        tinyrpc::FdEvent *e2 = container.getOrCreate(42);
        if (e1 == nullptr || e1 != e2) {
            std::cerr << "[fdevent] FAIL: getOrCreate returned different objects" << std::endl;
            return 1;
        }
        if (e1->getFd() != 42) {
            std::cerr << "[fdevent] FAIL: getOrCreate fd mismatch" << std::endl;
            return 1;
        }

        // 2) remove 后容器不持有悬空事件
        container.remove(42);
        if (container.get(42) != nullptr) {
            std::cerr << "[fdevent] FAIL: get after remove should be nullptr" << std::endl;
            return 1;
        }

        // 3) hook 能自动绑定当前 Reactor
        tinyrpc::Reactor reactor;
        tinyrpc::Reactor::setCurrentReactor(&reactor);
        tinyrpc::FdEvent *e3 = container.getOrCreate(99);
        if (e3 == nullptr || e3->getReactor() != &reactor) {
            std::cerr << "[fdevent] FAIL: auto-bind reactor mismatch" << std::endl;
            return 1;
        }
        container.remove(99);
        tinyrpc::Reactor::setCurrentReactor(nullptr);

        // 4) registerFdEvent 后 get 返回注册的指针
        tinyrpc::FdEvent externalEvt(77);
        bool registered = container.registerFdEvent(&externalEvt);
        if (!registered) {
            std::cerr << "[fdevent] FAIL: registerFdEvent should return true" << std::endl;
            return 1;
        }
        if (container.get(77) != &externalEvt) {
            std::cerr << "[fdevent] FAIL: get after registerFdEvent mismatch" << std::endl;
            return 1;
        }

        // 5) 不同 Reactor 中 fd 归属不会串线：重复 registerFdEvent 同一 fd 返回 false
        tinyrpc::FdEvent duplicateEvt(77);
        bool dupResult = container.registerFdEvent(&duplicateEvt);
        if (dupResult) {
            std::cerr << "[fdevent] FAIL: duplicate registerFdEvent should return false" << std::endl;
            return 1;
        }
        // 仍然是第一个注册的指针
        if (container.get(77) != &externalEvt) {
            std::cerr << "[fdevent] FAIL: registerFdEvent overwrote existing entry" << std::endl;
            return 1;
        }

        container.remove(77);
    }

    // ────────────────────────────────────────────
    // 全部通过
    // ────────────────────────────────────────────
    std::cout << "[fdevent] PASS" << std::endl;
    return 0;
}
