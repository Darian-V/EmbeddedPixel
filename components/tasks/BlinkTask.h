#pragma once
#include "IGpio.h"
#include "Thread.h"
#include <cstdint>

namespace app {

class BlinkTask : public osal::Runnable {
public:
    BlinkTask(hal::IGpio& led, uint32_t blinkPeriodMs);
    
    // The main execution loop
    void run() override;

private:
    hal::IGpio& m_led;
    uint32_t m_blinkPeriodMs;
};

} // namespace app
