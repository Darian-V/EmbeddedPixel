#pragma once
#include "IUart.h"
#include "stm32h7rsxx_hal.h"

namespace stm32 {
namespace h7 {

/**
 * @brief IUart implementation for STM32H7RS using the ST HAL.
 *
 * Wraps a single USART/UART peripheral. GPIO and clock configuration
 * is handled externally (typically in board_init or HAL_MSP callbacks).
 */
class Stm32H7Uart : public hal::IUart {
public:
    /**
     * @param instance  USART peripheral base (e.g., USART3)
     * @param baud_rate Desired baud rate (e.g., 115200)
     */
    Stm32H7Uart(USART_TypeDef* instance, uint32_t baud_rate);

    /** Initialize the UART peripheral. Call after clocks and GPIOs are configured. */
    bool init();

    bool transmit(const uint8_t* data, size_t length) override;
    bool receive(uint8_t* data, size_t length) override;

    UART_HandleTypeDef* getHandle() { return &huart_; }

private:
    UART_HandleTypeDef huart_;
};

} // namespace h7
} // namespace stm32
