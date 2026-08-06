#pragma once
#include <cstdint>

namespace osal {

class Mutex {
public:
    virtual ~Mutex() = default;

    // 0xFFFFFFFF typically represents infinite wait in FreeRTOS
    virtual bool lock(uint32_t timeoutMs = 0xFFFFFFFF) = 0; 
    virtual void unlock() = 0;
};

// Convenient RAII wrapper for Mutexes
class MutexLock {
public:
    explicit MutexLock(Mutex& mutex) : m_mutex(mutex) {
        m_mutex.lock();
    }
    ~MutexLock() {
        m_mutex.unlock();
    }

    MutexLock(const MutexLock&) = delete;
    MutexLock& operator=(const MutexLock&) = delete;
private:
    Mutex& m_mutex;
};

} // namespace osal
