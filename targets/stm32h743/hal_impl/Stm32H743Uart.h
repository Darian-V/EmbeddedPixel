#pragma once
#include "IUart.h"
#include "stm32h7xx_hal.h"

namespace stm32 {
namespace h743 {

/**
 * @brief IUart implementation for STM32H743 using the ST HAL.
 *
 * Wraps a single USART/UART peripheral. GPIO and clock configuration
 * is handled externally (typically in board_init or HAL_MSP callbacks).
 */
class Stm32H743Uart : public hal::IUart {
public:
    /**
     * @param instance  USART peripheral base (e.g., USART3 or USART1)
     * @param baud_rate Desired baud rate (e.g., 115200)
     */
    Stm32H743Uart(USART_TypeDef* instance, uint32_t baud_rate);

    /** Initialize the UART peripheral. Call after clocks and GPIOs are configured. */
    bool init();

    bool transmit(const uint8_t* data, size_t length) override;
    bool receive(uint8_t* data, size_t length) override;

    UART_HandleTypeDef* getHandle() { return &huart_; }

private:
    UART_HandleTypeDef huart_;
};

} // namespace h743
} // namespace stm32
