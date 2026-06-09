#pragma once

#include <condition_variable>
#include <cstddef>
#include <deque>
#include <functional>
#include <mutex>
#include <thread>
#include <vector>

namespace tinyrpc {

class ThreadPool {
 public:
    using Task = std::function<void()>;

    explicit ThreadPool(std::size_t threadNum);
    ~ThreadPool();

    ThreadPool(const ThreadPool&) = delete;
    ThreadPool& operator=(const ThreadPool&) = delete;

    bool start();
    void stop();
    bool addTask(Task task);

    std::size_t getThreadNum() const;
    bool isRunning() const;

 private:
    void runWorker();

    std::size_t m_threadNum {0};
    mutable std::mutex m_mutex;
    std::condition_variable m_condition;
    std::deque<Task> m_tasks;
    std::vector<std::thread> m_threads;
    bool m_started {false};
    bool m_stopping {false};
    bool m_stopped {false};
};

}
