#include "Stm32H743Uart.h"

namespace stm32 {
namespace h743 {

Stm32H743Uart::Stm32H743Uart(USART_TypeDef* instance, uint32_t baud_rate) {
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

bool Stm32H743Uart::init() {
    return HAL_UART_Init(&huart_) == HAL_OK;
}

bool Stm32H743Uart::transmit(const uint8_t* data, size_t length) {
    return HAL_UART_Transmit(&huart_, data, static_cast<uint16_t>(length), HAL_MAX_DELAY) == HAL_OK;
}

bool Stm32H743Uart::receive(uint8_t* data, size_t length) {
    return HAL_UART_Receive(&huart_, data, static_cast<uint16_t>(length), HAL_MAX_DELAY) == HAL_OK;
}

} // namespace h743
} // namespace stm32
