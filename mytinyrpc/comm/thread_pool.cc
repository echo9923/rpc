#include "comm/thread_pool.h"

#include <utility>

namespace tinyrpc {

ThreadPool::ThreadPool(std::size_t threadNum)
    : m_threadNum(threadNum)
{
}

ThreadPool::~ThreadPool()
{
    stop();
}

bool ThreadPool::start()
{
    std::lock_guard<std::mutex> lock(m_mutex);
    if (m_threadNum == 0 || m_started || m_stopping || m_stopped) {
        return false;
    }

    m_started = true;
    m_threads.reserve(m_threadNum);
    for (std::size_t i = 0; i < m_threadNum; ++i) {
        m_threads.emplace_back([this]() {
            runWorker();
        });
    }
    return true;
}

void ThreadPool::stop()
{
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (m_stopped) {
            return;
        }
        m_stopping = true;
        m_condition.notify_all();
    }

    for (auto& thread : m_threads) {
        if (thread.joinable()) {
            thread.join();
        }
    }

    std::lock_guard<std::mutex> lock(m_mutex);
    m_tasks.clear();
    m_threads.clear();
    m_started = false;
    m_stopping = false;
    m_stopped = true;
}

bool ThreadPool::addTask(Task task)
{
    if (!task) {
        return false;
    }

    std::lock_guard<std::mutex> lock(m_mutex);
    if (!m_started || m_stopping || m_stopped) {
        return false;
    }

    m_tasks.push_back(std::move(task));
    m_condition.notify_one();
    return true;
}

std::size_t ThreadPool::getThreadNum() const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_threadNum;
}

bool ThreadPool::isRunning() const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_started && !m_stopping && !m_stopped;
}

void ThreadPool::runWorker()
{
    while (true) {
        Task task;
        {
            std::unique_lock<std::mutex> lock(m_mutex);
            m_condition.wait(lock, [this]() {
                return m_stopping || !m_tasks.empty();
            });
            if (m_stopping && m_tasks.empty()) {
                return;
            }

            task = std::move(m_tasks.front());
            m_tasks.pop_front();
        }

        task();
    }
}

}
