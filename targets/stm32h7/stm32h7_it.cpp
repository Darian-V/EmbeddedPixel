#include "stm32h7rsxx_hal.h"
#ifdef USE_FREERTOS
#include "FreeRTOS.h"
#include "task.h"
#endif

extern "C" {

void Error_Handler(void) {
    __disable_irq();
    while (1) {}
}

#ifdef USE_FREERTOS
extern void xPortSysTickHandler(void);
#endif

void SysTick_Handler(void) {
    // 1. Increment ST HAL tick (needed for HAL_Delay/timeouts during boot)
    HAL_IncTick();

#ifdef USE_FREERTOS
    // 2. If FreeRTOS scheduler has started, call its tick handler
    if (xTaskGetSchedulerState() != taskSCHEDULER_NOT_STARTED) {
        xPortSysTickHandler();
    }
#endif
}

} // extern "C"
