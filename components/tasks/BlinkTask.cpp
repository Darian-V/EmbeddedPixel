#include "BlinkTask.h"

namespace app {

BlinkTask::BlinkTask(hal::IGpio& led, uint32_t blinkPeriodMs)
    : m_led(led), m_blinkPeriodMs(blinkPeriodMs) {
}

void BlinkTask::run() {
    // Hardware-agnostic infinite loop
    while (true) {
        m_led.toggle();
        osal::Thread::delay(m_blinkPeriodMs);
    }
}

} // namespace app
