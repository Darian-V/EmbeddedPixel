#include "BlinkTask.h"

namespace app {

BlinkTask::BlinkTask(hal::IGpio& led, uint32_t blinkPeriodMs)
    : m_led(led), m_blinkPeriodMs(blinkPeriodMs) {
}

void BlinkTask::run() {
    while (true) {
        m_led.toggle();
        uint32_t delayMs = m_blinkPeriodMs;
        if (delayMs < 10) delayMs = 10;
        osal::Thread::delay(delayMs);
    }
}

} // namespace app
