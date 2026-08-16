#include <stdio.h>

#include "board_init.h"
#include "BlinkTask.h"
#include "TempTask.h"
#include "FreeRtosThread.h"
#include "console.h"
#include "stm32h7rsxx_hal.h"

int main(void) {
    Board_Init();
    console_init(Board_GetDebugUart());

    printf("\r\n=== STM32H7RS DTS Temperature Test ===\r\n");

    static hal::IGpio& led = Board_GetLed();
    static app::BlinkTask blinky(led, 500);
    static stm32::FreeRtosThread blink_thread(blinky, "BlinkTask", 256, 3);

    static hal::ITempSensor& temp_sensor = Board_GetTempSensor();
    static app::TempTask temp_task(temp_sensor, 1000); // 1Hz default polling
    static stm32::FreeRtosThread temp_thread(temp_task, "TempTask", 512, 3);

    blink_thread.start();
    temp_thread.start();

    vTaskStartScheduler();

    while (1) {}
}
