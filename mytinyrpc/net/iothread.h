#pragma once

#include "net/reactor.h"

#include <atomic>
#include <functional>
#include <thread>

namespace tinyrpc {

class IOThread {
 public:
    IOThread();
    ~IOThread();

    IOThread(const IOThread&) = delete;
    IOThread& operator=(const IOThread&) = delete;

    Reactor* getReactor();
    const Reactor* getReactor() const;
    std::thread::id getThreadId() const;
    bool isStarted() const;

    void addTask(std::function<void()> task);
    void stop();

 private:
    void threadFunc();

    Reactor m_reactor;
    std::thread m_thread;
    std::atomic<bool> m_started {false};
    std::thread::id m_threadId;
};

}
