#include "net/mutex.h"

namespace tinyrpc {

void Mutex::lock()
{
    m_mutex.lock();
}

void Mutex::unlock()
{
    m_mutex.unlock();
}

bool Mutex::tryLock()
{
    return m_mutex.try_lock();
}

MutexLockGuard::MutexLockGuard(Mutex& mutex)
    : m_mutex(mutex)
{
    m_mutex.lock();
}

MutexLockGuard::~MutexLockGuard()
{
    m_mutex.unlock();
}

void RWMutex::readLock()
{
    m_mutex.lock_shared();
}

void RWMutex::readUnlock()
{
    m_mutex.unlock_shared();
}

void RWMutex::writeLock()
{
    m_mutex.lock();
}

void RWMutex::writeUnlock()
{
    m_mutex.unlock();
}

ReadLockGuard::ReadLockGuard(RWMutex& mutex)
    : m_mutex(mutex)
{
    m_mutex.readLock();
}

ReadLockGuard::~ReadLockGuard()
{
    m_mutex.readUnlock();
}

WriteLockGuard::WriteLockGuard(RWMutex& mutex)
    : m_mutex(mutex)
{
    m_mutex.writeLock();
}

WriteLockGuard::~WriteLockGuard()
{
    m_mutex.writeUnlock();
}

}
