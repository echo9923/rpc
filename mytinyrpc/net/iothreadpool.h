#pragma once

#include "net/iothread.h"

#include <cstddef>
#include <functional>
#include <memory>
#include <vector>

namespace tinyrpc {

class IOThreadPool {
 public:
    explicit IOThreadPool(std::size_t size);
    ~IOThreadPool();

    IOThreadPool(const IOThreadPool&) = delete;
    IOThreadPool& operator=(const IOThreadPool&) = delete;

    std::size_t getSize() const;
    IOThread* getNextIOThread();
    IOThread* getIOThreadByIndex(std::size_t index);
    const IOThread* getIOThreadByIndex(std::size_t index) const;

    void broadcastTask(const std::function<void()>& task);
    bool addTaskByIndex(std::size_t index, std::function<void()> task);
    void stop();

 private:
    std::vector<std::unique_ptr<IOThread>> m_threads;
    std::size_t m_nextIndex {0};
};

}
