#include "board_init.h"
#include "stm32h7xx_hal.h"
#include "Thread.h"
#include "FreeRtosThread.h"
#include "console.h"
#include <stdio.h>

class UartDebugTask : public osal::Runnable {
public:
    UartDebugTask(hal::IGpio& led, hal::IUart& uart)
        : led_(led), uart_(uart), count_(0) {}

    void run() override {
        while (true) {
            led_.toggle();
            count_++;
            printf("[UARTDebug] Heartbeat #%lu - status LED toggled\r\n", count_);
            vTaskDelay(pdMS_TO_TICKS(1000));
        }
    }

private:
    hal::IGpio& led_;
    hal::IUart& uart_;
    uint32_t count_;
};

int main(void) {
    // 1. Initialize board hardware, clocks, MPU, and debug UART
    Board_Init();

    // 2. Initialize console redirect (printf -> USART1)
    console_init(Board_GetDebugUart());

    // 3. Direct UART transmit test
    const char msg[] = "\r\nHello World from PixelJam Direct UART Transmit!\r\n";
    Board_GetDebugUart().transmit(reinterpret_cast<const uint8_t*>(msg), sizeof(msg) - 1);

    // 4. Console printf test
    printf("\r\n==============================================\r\n");
    printf("  PixelJam Board UARTDebug App (STM32H743)  \r\n");
    printf("  USART1 @ 115200 8N1 (PB6 TX / PB7 RX)      \r\n");
    printf("==============================================\r\n\r\n");

    // 5. Start tasks
    static hal::IGpio& statusLed = Board_GetLed();
    static UartDebugTask debugTask(statusLed, Board_GetDebugUart());
    static stm32::FreeRtosThread debugThread(debugTask, "UartDebugTask", 512, 3);

    debugThread.start();
    vTaskStartScheduler();

    while (1) {}
    return 0;
}
