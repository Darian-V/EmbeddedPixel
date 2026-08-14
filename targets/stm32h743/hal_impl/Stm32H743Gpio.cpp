#include "Stm32H743Gpio.h"
#include "stm32h7xx_hal.h"

namespace stm32 {
namespace h743 {

Stm32H743Gpio::Stm32H743Gpio(GPIO_TypeDef* port, uint16_t pin) 
    : m_port(port), m_pin(pin) {
}

void Stm32H743Gpio::set() {
    HAL_GPIO_WritePin(m_port, m_pin, GPIO_PIN_SET);
}

void Stm32H743Gpio::reset() {
    HAL_GPIO_WritePin(m_port, m_pin, GPIO_PIN_RESET);
}

void Stm32H743Gpio::toggle() {
    HAL_GPIO_TogglePin(m_port, m_pin);
}

bool Stm32H743Gpio::read() {
    return HAL_GPIO_ReadPin(m_port, m_pin) == GPIO_PIN_SET;
}

} // namespace h743
} // namespace stm32
