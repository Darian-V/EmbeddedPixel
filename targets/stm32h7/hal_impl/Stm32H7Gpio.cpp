#include "Stm32H7Gpio.h"
#include "stm32h7rsxx_hal.h" // ST Vendor HAL Header for H7RS

namespace stm32 {
namespace h7 {

Stm32H7Gpio::Stm32H7Gpio(GPIO_TypeDef* port, uint16_t pin) 
    : m_port(port), m_pin(pin) {
}

void Stm32H7Gpio::set() {
    HAL_GPIO_WritePin(m_port, m_pin, GPIO_PIN_SET);
}

void Stm32H7Gpio::reset() {
    HAL_GPIO_WritePin(m_port, m_pin, GPIO_PIN_RESET);
}

void Stm32H7Gpio::toggle() {
    HAL_GPIO_TogglePin(m_port, m_pin);
}

bool Stm32H7Gpio::read() {
    return HAL_GPIO_ReadPin(m_port, m_pin) == GPIO_PIN_SET;
}

} // namespace h7
} // namespace stm32
