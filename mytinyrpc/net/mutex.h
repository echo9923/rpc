#pragma once

#include <mutex>
#include <shared_mutex>

namespace tinyrpc {

class Mutex {
 public:
    Mutex() = default;
    ~Mutex() = default;

    Mutex(const Mutex&) = delete;
    Mutex& operator=(const Mutex&) = delete;

    void lock();
    void unlock();
    bool tryLock();

 private:
    std::mutex m_mutex;
};

class MutexLockGuard {
 public:
    explicit MutexLockGuard(Mutex& mutex);
    ~MutexLockGuard();

    MutexLockGuard(const MutexLockGuard&) = delete;
    MutexLockGuard& operator=(const MutexLockGuard&) = delete;

 private:
    Mutex& m_mutex;
};

class RWMutex {
 public:
    RWMutex() = default;
    ~RWMutex() = default;

    RWMutex(const RWMutex&) = delete;
    RWMutex& operator=(const RWMutex&) = delete;

    void readLock();
    void readUnlock();
    void writeLock();
    void writeUnlock();

 private:
    std::shared_mutex m_mutex;
};

class ReadLockGuard {
 public:
    explicit ReadLockGuard(RWMutex& mutex);
    ~ReadLockGuard();

    ReadLockGuard(const ReadLockGuard&) = delete;
    ReadLockGuard& operator=(const ReadLockGuard&) = delete;

 private:
    RWMutex& m_mutex;
};

class WriteLockGuard {
 public:
    explicit WriteLockGuard(RWMutex& mutex);
    ~WriteLockGuard();

    WriteLockGuard(const WriteLockGuard&) = delete;
    WriteLockGuard& operator=(const WriteLockGuard&) = delete;

 private:
    RWMutex& m_mutex;
};

}
