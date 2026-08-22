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

    void set_period_ms(uint32_t periodMs) { m_blinkPeriodMs = periodMs; }
    uint32_t get_period_ms() const { return m_blinkPeriodMs; }

private:
    hal::IGpio& m_led;
    volatile uint32_t m_blinkPeriodMs;
};

} // namespace app
