#include "board_init.h"
#include "stm32h7rsxx_hal.h"
#include "BlinkTask.h"
#include "Stm32H7Gpio.h"
#include "FreeRtosThread.h"
#include "console.h"
#include <stdio.h>

int main(void) {
    // 1. Initialize the board (Hardware specific)
    Board_Init();
    console_init();

    printf("\r\n=== Blinky Application Started! ===\r\n");

    // 2. Enable the GPIO Clock for Port D (where the Green LED is on Nucleo)
    __HAL_RCC_GPIOD_CLK_ENABLE();

    // 3. Configure the GPIO pin (PD10)
    GPIO_InitTypeDef GPIO_InitStruct = {0};
    GPIO_InitStruct.Pin = GPIO_PIN_10;
    GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
    GPIO_InitStruct.Pull = GPIO_NOPULL;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
    HAL_GPIO_Init(GPIOD, &GPIO_InitStruct);

    // 4. Instantiate our architecture
    // Objects must be static to survive the FreeRTOS MSP reset!
    static stm32::h7::Stm32H7Gpio greenLed(GPIOD, GPIO_PIN_10);
    static app::BlinkTask blinky(greenLed, 500); 
    static stm32::FreeRtosThread blinkThread(blinky, "BlinkTask", 256, 3);
    
    blinkThread.start();
    vTaskStartScheduler();

    // We should never reach here
    return 0;
}
