#include "net/iothreadpool.h"

#include <algorithm>
#include <utility>

namespace tinyrpc {

IOThreadPool::IOThreadPool(std::size_t size)
{
    m_threads.reserve(size);
    for (std::size_t i = 0; i < size; ++i) {
        m_threads.push_back(std::make_unique<IOThread>());
    }
}

IOThreadPool::~IOThreadPool()
{
    stop();
}

std::size_t IOThreadPool::getSize() const
{
    return m_threads.size();
}

IOThread* IOThreadPool::getNextIOThread()
{
    if (m_threads.empty()) {
        return nullptr;
    }

    IOThread *thread = m_threads[m_nextIndex].get();
    m_nextIndex = (m_nextIndex + 1) % m_threads.size();
    return thread;
}

IOThread* IOThreadPool::getIOThreadByIndex(std::size_t index)
{
    if (index >= m_threads.size()) {
        return nullptr;
    }
    return m_threads[index].get();
}

const IOThread* IOThreadPool::getIOThreadByIndex(std::size_t index) const
{
    if (index >= m_threads.size()) {
        return nullptr;
    }
    return m_threads[index].get();
}

void IOThreadPool::broadcastTask(const std::function<void()>& task)
{
    if (!task) {
        return;
    }

    for (auto& thread : m_threads) {
        thread->addTask(task);
    }
}

bool IOThreadPool::addTaskByIndex(std::size_t index, std::function<void()> task)
{
    IOThread *thread = getIOThreadByIndex(index);
    if (thread == nullptr || !task) {
        return false;
    }

    thread->addTask(std::move(task));
    return true;
}

void IOThreadPool::stop()
{
    for (auto& thread : m_threads) {
        if (thread != nullptr) {
            thread->stop();
        }
    }
}

}
