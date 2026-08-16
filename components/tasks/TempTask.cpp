#include "TempTask.h"

#include <stdio.h>

namespace app {

TempTask::TempTask(hal::ITempSensor& sensor, uint32_t period_ms)
    : sensor_(sensor), period_ms_(period_ms) {
}

void TempTask::run() {
    if (!sensor_.init()) {
        printf("[TEMP] Sensor initialization failed!\r\n");
        return;
    }
    printf("[TEMP] On-chip temperature sensor initialized (polling every %lu ms)\r\n", static_cast<unsigned long>(period_ms_));

    while (true) {
        int32_t temp_c = 0;
        if (sensor_.get_temperature(temp_c)) {
            printf("[TEMP] On-chip Temperature: %ld C\r\n", static_cast<long>(temp_c));
        } else {
            printf("[TEMP] Failed to read temperature\r\n");
        }
        osal::Thread::delay(period_ms_);
    }
}

} // namespace app
