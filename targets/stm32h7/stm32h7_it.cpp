#include "stm32h7rsxx_hal.h"
#include "FreeRTOS.h"
#include "task.h"

extern "C" {

void Error_Handler(void) {
    __disable_irq();
    while (1) {}
}

extern void xPortSysTickHandler(void);

void SysTick_Handler(void) {
    // 1. Increment ST HAL tick (needed for HAL_Delay/timeouts during boot)
    HAL_IncTick();

    // 2. If FreeRTOS scheduler has started, call its tick handler
    if (xTaskGetSchedulerState() != taskSCHEDULER_NOT_STARTED) {
        xPortSysTickHandler();
    }
}

} // extern "C"
