#include "BlinkTask.h"

namespace app {

BlinkTask::BlinkTask(hal::IGpio& led, uint32_t blinkPeriodMs)
    : m_led(led), m_blinkPeriodMs(blinkPeriodMs) {
}

void BlinkTask::run() {
    bool isOn = false;
    while (true) {
        m_led.toggle();
        isOn = !isOn;
        if (isOn) {
            osal::Thread::delay(1000);
        } else {
            osal::Thread::delay(4000);
        }
    }
}

} // namespace app
