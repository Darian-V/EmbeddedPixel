#include "Stm32H7Uart.h"

namespace stm32 {
namespace h7 {

Stm32H7Uart::Stm32H7Uart(USART_TypeDef* instance, uint32_t baud_rate) {
    huart_ = {};
    huart_.Instance = instance;
    huart_.Init.BaudRate = baud_rate;
    huart_.Init.WordLength = UART_WORDLENGTH_8B;
    huart_.Init.StopBits = UART_STOPBITS_1;
    huart_.Init.Parity = UART_PARITY_NONE;
    huart_.Init.Mode = UART_MODE_TX_RX;
    huart_.Init.HwFlowCtl = UART_HWCONTROL_NONE;
    huart_.Init.OverSampling = UART_OVERSAMPLING_16;
    huart_.Init.OneBitSampling = UART_ONE_BIT_SAMPLE_DISABLE;
    huart_.Init.ClockPrescaler = UART_PRESCALER_DIV1;
    huart_.AdvancedInit.AdvFeatureInit = UART_ADVFEATURE_NO_INIT;
}

bool Stm32H7Uart::init() {
    return HAL_UART_Init(&huart_) == HAL_OK;
}

bool Stm32H7Uart::transmit(const uint8_t* data, size_t length) {
    return HAL_UART_Transmit(&huart_, data, static_cast<uint16_t>(length), HAL_MAX_DELAY) == HAL_OK;
}

bool Stm32H7Uart::receive(uint8_t* data, size_t length) {
    return HAL_UART_Receive(&huart_, data, static_cast<uint16_t>(length), HAL_MAX_DELAY) == HAL_OK;
}

bool Stm32H7Uart::receive_byte(uint8_t& byte, uint32_t timeout_ms) {
    return HAL_UART_Receive(&huart_, &byte, 1, timeout_ms) == HAL_OK;
}

} // namespace h7
} // namespace stm32
