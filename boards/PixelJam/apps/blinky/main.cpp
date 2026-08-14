#include "board_init.h"
#include "stm32h7xx_hal.h"
#include "BlinkTask.h"
#include "FreeRtosThread.h"
#include "console.h"
#include <stdio.h>

int main(void) {
    // 1. Initialize board hardware, clocks, MPU, and debug UART
    Board_Init();
    console_init(Board_GetDebugUart());

    printf("\r\n=== PixelJam Blinky Application Started (STM32H743) ===\r\n");

    // 2. Instantiate architecture with board status LED
    static hal::IGpio& statusLed = Board_GetLed();
    static app::BlinkTask blinky(statusLed, 500); 
    static stm32::FreeRtosThread blinkThread(blinky, "BlinkTask", 256, 3);
    
    blinkThread.start();
    vTaskStartScheduler();

    // Should never reach here
    while (1) {}
    return 0;
}
