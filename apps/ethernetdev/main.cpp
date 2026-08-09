#include "board_init.h"
#include "BlinkTask.h"
#include "FreeRtosThread.h"
#include "console.h"
#include "NetManager.h"
#include "Stm32H7Eth.h"
#include <stdio.h>

int main(void) {
    // 1. Initialize the board (hardware-specific, opaque to us)
    Board_Init();
    console_init();

    printf("\r\n=== EthernetDev Application Started! ===\r\n");

    // 2. Get board resources — pin/clock setup happens inside Board_GetLed()
    static hal::IGpio& led = Board_GetLed();

    // 3. Instantiate application components
    static app::BlinkTask blinky(led, 500);
    static stm32::FreeRtosThread blinkThread(blinky, "BlinkTask", 256, 3);

    extern IEth* g_eth_driver; // from ethernetif.cpp
    static Stm32H7Eth ethDriver;
    g_eth_driver = &ethDriver;

    static net::NetManager netMan(ethDriver);
    static stm32::FreeRtosThread netThread(netMan, "NetManager", 1024, 4);

    blinkThread.start();
    netThread.start();

    vTaskStartScheduler();

    // Should never reach here
    return 0;
}
