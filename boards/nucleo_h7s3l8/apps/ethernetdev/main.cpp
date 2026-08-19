#include "board_init.h"
#include "BlinkTask.h"
#include "FreeRtosThread.h"
#include "console.h"
#include "NetManager.h"
#include "Stm32H7Eth.h"
#include "Lan8742Phy.h"
#include "stm32h7rsxx_hal.h"
#include <stdio.h>

#include "DiscoveryService.h"
#include "TelemetryService.h"
#include "CommandService.h"
#include "OtaService.h"
#include "Stm32ExtMemFlash.h"
#include "ITelemetryChannel.h"

int main(void) {
    Board_Init();
    console_init(Board_GetDebugUart());

#if defined(FW_V2) || defined(ETHERNETDEV_V2)
    constexpr uint32_t CURRENT_FW_VERSION = 0x00010100; // v1.1.0
    printf("\r\n=== EthernetDev v1.1.0 (Red LED PB7 @ 200ms) ===\r\n");
    static hal::IGpio& led = Board_GetRedLed();
    static app::BlinkTask blinky(led, 200);
#else
    constexpr uint32_t CURRENT_FW_VERSION = 0x00010000; // v1.0.0
    printf("\r\n=== EthernetDev v1.0.0 (Green LED PD10 @ 500ms) ===\r\n");
    static hal::IGpio& led = Board_GetGreenLed();
    static app::BlinkTask blinky(led, 500);
#endif

    static stm32::FreeRtosThread blinkThread(blinky, "BlinkTask", 256, 3);

    // ── Ethernet driver configuration ──────────────────────────────────────
    static Lan8742Phy phy(0);  // PHY address 0 on Nucleo-H7S3L8

    static const Stm32H7EthConfig ethCfg = {
        .mac_addr        = {0x00, 0x80, 0xE1, 0x11, 0x22, 0x33},
        .media_interface = HAL_ETH_RMII_MODE,
    };

    static Stm32H7Eth ethDriver(ethCfg, phy);

    // ── IP stack configuration ─────────────────────────────────────────────
    constexpr uint16_t NODE_ID = 1;

    static net::IpConfig ipCfg;
    ipCfg.mode            = net::IpMode::DHCP_WITH_FALLBACK;
    ipCfg.static_ip       = net::IP4_MAKE(192, 168, 1, 111);
    ipCfg.netmask         = net::IP4_MAKE(255, 255, 255, 0);
    ipCfg.gateway         = net::IP4_MAKE(192, 168, 1, 1);
    ipCfg.dhcp_timeout_ms = 10000;
    ipCfg.hostname        = "embeddedpixel";

    static net::NetManager netMan(ethDriver, ipCfg);
    static stm32::FreeRtosThread netThread(netMan, "NetManager", 1024, 4);

    // ── Board-Specific Telemetry Channels ──────────────────────────────────
    static net::CounterChannel counterChannel(10); // 10Hz Monotonic Counter ('CNTR')
    static hal::ITempSensor& tempSensor = Board_GetTempSensor();
    static net::TemperatureChannel tempChannel(tempSensor, 1); // 1Hz DTS Temperature ('TEMP')

    // ── Flash Driver & OTA Service ─────────────────────────────────────────
    static stm32::h7::Stm32ExtMemFlash flashDriver;
    flashDriver.init();
    static net::services::OtaService otaService(flashDriver);

    // ── Network Services ───────────────────────────────────────────────────
    static net::services::DiscoveryService discoveryService(netMan, NODE_ID, &tempSensor, CURRENT_FW_VERSION);
    static stm32::FreeRtosThread discoveryThread(discoveryService, "DiscoverySvc", 1024, 2);

    static net::services::TelemetryService telemetryService(netMan, NODE_ID, counterChannel);
    telemetryService.register_channel(tempChannel);
    static stm32::FreeRtosThread telemetryThread(telemetryService, "TelemetrySvc", 2048, 4);

    static net::services::CommandService commandService(netMan, discoveryService, telemetryService, NODE_ID, &otaService);
    static stm32::FreeRtosThread commandThread(commandService, "CommandSvc", 2048, 3);

    blinkThread.start();
    netThread.start();
    discoveryThread.start();
    telemetryThread.start();
    commandThread.start();

    vTaskStartScheduler();

    while (1) {}
}
