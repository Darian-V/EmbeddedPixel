#include "board_init.h"
#include "BlinkTask.h"
#include "FreeRtosThread.h"
#include "console.h"
#include "NetManager.h"
#include "Stm32H7Eth.h"
#include "Lan8742Phy.h"
#include "stm32h7rsxx_hal.h"
#include <stdio.h>

int main(void) {
    Board_Init();
    console_init();

    printf("\r\n=== EthernetDev ===\r\n");

    static hal::IGpio& led = Board_GetLed();
    static app::BlinkTask blinky(led, 500);
    static stm32::FreeRtosThread blinkThread(blinky, "BlinkTask", 256, 3);

    // ── Ethernet driver configuration ──────────────────────────────────────
    static Lan8742Phy phy(0);  // PHY address 0 on Nucleo-H7S3L8

    static const Stm32H7EthConfig ethCfg = {
        .mac_addr        = {0x00, 0x80, 0xE1, 0x11, 0x22, 0x33},
        .media_interface = HAL_ETH_RMII_MODE,
    };

    static Stm32H7Eth ethDriver(ethCfg, phy);

    // ── IP stack configuration ─────────────────────────────────────────────
    static net::IpConfig ipCfg;
    ipCfg.mode            = net::IpMode::DHCP_WITH_FALLBACK;
    ipCfg.static_ip       = net::IP4_MAKE(192, 168, 1, 111);
    ipCfg.netmask         = net::IP4_MAKE(255, 255, 255, 0);
    ipCfg.gateway         = net::IP4_MAKE(192, 168, 1, 1);
    ipCfg.dhcp_timeout_ms = 10000;
    ipCfg.hostname        = "embeddedpixel";

    static net::NetManager netMan(ethDriver, ipCfg);
    static stm32::FreeRtosThread netThread(netMan, "NetManager", 1024, 4);

    blinkThread.start();
    netThread.start();

    vTaskStartScheduler();

    while (1) {}
}
