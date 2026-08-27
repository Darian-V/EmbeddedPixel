#pragma once

#include "ICan.h"
#include "stm32h7rsxx_hal.h"

namespace stm32::h7 {

/**
 * @brief hal::ICan implementation for STM32H7RS using ST FDCAN HAL.
 *
 * Configured in Classic CAN 2.0B mode with 29-bit Extended ID support.
 */
class Stm32H7Can : public hal::ICan {
public:
    explicit Stm32H7Can(FDCAN_GlobalTypeDef* instance = FDCAN1);
    ~Stm32H7Can() override;

    bool init(hal::CanBaudRate baud = hal::CanBaudRate::Baud500k) override;
    bool transmit(const hal::CanFrame& frame, uint32_t timeout_ms = 10) override;
    bool receive(hal::CanFrame& frame, uint32_t timeout_ms = 0) override;
    bool configure_filter(const hal::CanFilter& filter) override;
    bool is_bus_off() const override;
    void recover_bus() override;

    FDCAN_HandleTypeDef* get_handle() { return &hfdcan_; }

private:
    FDCAN_HandleTypeDef hfdcan_;
    bool                 initialized_{false};

    void configure_bit_timing(hal::CanBaudRate baud);
};

} // namespace stm32::h7
