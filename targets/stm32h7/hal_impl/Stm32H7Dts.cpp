#include "Stm32H7Dts.h"

#include <cstring>
#include <stdio.h>

namespace stm32::h7 {

Stm32H7Dts::Stm32H7Dts()
    : hdts_{}, is_initialized_(false), cached_temp_c_(25), last_read_tick_(0) {
}

Stm32H7Dts::~Stm32H7Dts() {
    stop();
}

bool Stm32H7Dts::init() {
    if (is_initialized_) {
        return true;
    }

    __HAL_RCC_DTS_CLK_ENABLE();

    std::memset(&hdts_, 0, sizeof(hdts_));
    hdts_.Instance = DTS;
    hdts_.Init.QuickMeasure = DTS_QUICKMEAS_DISABLE;
    hdts_.Init.RefClock = DTS_REFCLKSEL_PCLK;
    hdts_.Init.TriggerInput = DTS_TRIGGER_HW_NONE;
    hdts_.Init.SamplingTime = DTS_SMP_TIME_15_CYCLE;

    // Prescaler divider ensures counter clock is <= 1 MHz during calibration
    uint32_t pclk = HAL_RCC_GetPCLK1Freq();
    uint32_t div = (pclk > 1000000UL) ? (pclk / 1000000UL) : 1UL;
    if (div > 127UL) {
        div = 127UL;
    }
    hdts_.Init.Divider = div;
    hdts_.Init.HighThreshold = 0;
    hdts_.Init.LowThreshold = 0;

    if (HAL_DTS_Init(&hdts_) != HAL_OK) {
        return false;
    }

    if (HAL_DTS_Start(&hdts_) != HAL_OK) {
        return false;
    }

    is_initialized_ = true;
    return true;
}

bool Stm32H7Dts::get_temperature(int32_t& temp_c) {
    if (!is_initialized_) {
        temp_c = 25;
        return true;
    }

    uint32_t now = HAL_GetTick();
    if (now - last_read_tick_ >= 500 || last_read_tick_ == 0) {
        int32_t raw_t = 0;
        if (HAL_DTS_GetTemperature(&hdts_, &raw_t) == HAL_OK) {
            cached_temp_c_ = raw_t;
            last_read_tick_ = now;
        }
    }
    temp_c = cached_temp_c_;
    return true;
}

void Stm32H7Dts::stop() {
    if (is_initialized_) {
        HAL_DTS_Stop(&hdts_);
        is_initialized_ = false;
    }
}

} // namespace stm32::h7
