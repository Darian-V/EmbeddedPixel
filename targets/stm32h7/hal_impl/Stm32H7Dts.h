#pragma once

#include <cstdint>

#include "ITempSensor.h"
#include "stm32h7rsxx_hal.h"

namespace stm32::h7 {

class Stm32H7Dts : public hal::ITempSensor {
public:
    Stm32H7Dts();
    ~Stm32H7Dts() override;

    bool init() override;
    bool get_temperature(int32_t& temp_c) override;
    void stop() override;

private:
    DTS_HandleTypeDef hdts_;
    bool is_initialized_;
};

} // namespace stm32::h7
