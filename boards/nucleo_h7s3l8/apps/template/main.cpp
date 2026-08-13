#include "board_init.h"
#include "stm32h7rsxx_hal.h"
#include "console.h"
#include <stdio.h>
// Include FreeRTOS if using RTOS
#include "FreeRTOS.h"
#include "task.h"

int main(void) {
    // 1. Initialize the board hardware, clocks, and memory map
    Board_Init();
    console_init(Board_GetDebugUart());

    printf("\r\n=== Template Application Started ===\r\n");

    // 2. Add your application logic or RTOS Tasks here

    // 3. Start the FreeRTOS Scheduler (if using RTOS)
    vTaskStartScheduler();

    // 4. Fallback loop
    while (1) {
        HAL_Delay(1000);
        printf("Running...\r\n");
    }

    return 0;
}
