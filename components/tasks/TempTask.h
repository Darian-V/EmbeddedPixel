#pragma once

#include <cstdint>

#include "ITempSensor.h"
#include "Thread.h"

namespace app {

class TempTask : public osal::Runnable {
public:
    explicit TempTask(hal::ITempSensor& sensor, uint32_t period_ms = 1000);

    void run() override;

private:
    hal::ITempSensor& sensor_;
    uint32_t period_ms_;
};

} // namespace app
