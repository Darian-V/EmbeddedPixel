#pragma once
#include <cstdint>

namespace osal {

class Thread {
public:
    virtual ~Thread() = default;

    virtual bool start() = 0;
    virtual void suspend() = 0;
    virtual void resume() = 0;
    
    // System-wide delay
    static void delay(uint32_t milliseconds);
};

// Interface for objects that can be run by a thread
class Runnable {
public:
    virtual ~Runnable() = default;
    virtual void run() = 0;
};

} // namespace osal
