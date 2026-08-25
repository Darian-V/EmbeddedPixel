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
#include "Version.h"
#include "SystemController.h"
#include "CliEngine.h"
#include "ConsoleTask.h"

#if defined(FW_V2) || defined(ETHERNETDEV_V2)
    constexpr uint32_t CURRENT_FW_VERSION = sys::MAKE_VERSION(1, 1, 0, 0); // v1.1.0
    constexpr uint32_t DEFAULT_BLINK_RATE = 200;
#else
    constexpr uint32_t CURRENT_FW_VERSION = sys::MAKE_VERSION(1, 0, 0, 0); // v1.0.0
    constexpr uint32_t DEFAULT_BLINK_RATE = 500;
#endif

// ── Application Image Header (Placed at fixed flash offset 0x200 via .app_header)
__attribute__((section(".app_header"), used))
static const sys::AppImageHeader s_app_image_header = {
    .magic                  = sys::EPFW_MAGIC,
    .header_version         = sys::EPFW_HEADER_VERSION,
    .board_id               = static_cast<uint16_t>(sys::BoardId::NUCLEO_H7S3L8),
    .app_version            = CURRENT_FW_VERSION,
    .min_bootloader_version = sys::MAKE_VERSION(1, 0, 0, 0),
    .feature_flags          = static_cast<uint32_t>(
        sys::FeatureFlag::FEAT_ETHERNET_LAN8742 |
        sys::FeatureFlag::FEAT_TELEMETRY_STREAM |
        sys::FeatureFlag::FEAT_TEMP_SENSOR_DTS  |
        sys::FeatureFlag::FEAT_OTA_RAM_STAGING  |
        sys::FeatureFlag::FEAT_DYNAMIC_RATE     |
        sys::FeatureFlag::FEAT_UART_CLI
    ),
    .image_size             = 0,
    .image_crc32            = 0,
    .build_timestamp        = 0,
    .git_commit             = 0,
    .reserved               = {0},
    .header_crc32           = 0,
};

int main(void) {
    Board_Init();
    console_init(Board_GetDebugUart());

    char ver_str[24];
    sys::format_version(CURRENT_FW_VERSION, ver_str, sizeof(ver_str));

#if defined(FW_V2) || defined(ETHERNETDEV_V2)
    printf("\r\n=== EthernetDev %s (Red LED PB7 @ %lums) ===\r\n", ver_str, DEFAULT_BLINK_RATE);
    static hal::IGpio& led = Board_GetRedLed();
#else
    printf("\r\n=== EthernetDev %s (Green LED PD10 @ %lums) ===\r\n", ver_str, DEFAULT_BLINK_RATE);
    static hal::IGpio& led = Board_GetGreenLed();
#endif

    static app::BlinkTask blinky(led, DEFAULT_BLINK_RATE);
    static stm32::FreeRtosThread blinkThread(blinky, "BlinkTask", 256, 1);

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
    static net::StressTestChannel<16> str1Channel(net::proto::STREAM_TAG_STR1, 1000, 45); // 16ch @ 1000 Hz, batch 45 ('STR1')
    static net::StressTestChannel<64> str6Channel(net::proto::STREAM_TAG_STR6, 5000, 11); // 64ch @ 5000 Hz, batch 11 ('STR6')
    static net::StressTestChannel<64> raw0Channel(net::proto::STREAM_TAG_RAW0, 50000, 11); // 64ch @ 50000 Hz, batch 11 ('RAW0')

    // ── Flash Driver & OTA Service ─────────────────────────────────────────
    static stm32::h7::Stm32ExtMemFlash flashDriver;
    flashDriver.init();
    static net::services::OtaService otaService(flashDriver);

    // ── Network Services ───────────────────────────────────────────────────
    static net::services::DiscoveryService discoveryService(netMan, NODE_ID, &tempSensor, CURRENT_FW_VERSION);
    static stm32::FreeRtosThread discoveryThread(discoveryService, "DiscoverySvc", 1024, 2);

    static net::services::TelemetryService telemetryService(netMan, NODE_ID, counterChannel);
    telemetryService.register_channel(tempChannel);
    telemetryService.register_channel(str1Channel);
    telemetryService.register_channel(str6Channel);
    telemetryService.register_channel(raw0Channel);
    static stm32::FreeRtosThread telemetryThread(telemetryService, "TelemetrySvc", 2048, 3);

    static net::services::CommandService commandService(netMan, discoveryService, telemetryService, NODE_ID, &otaService);
    static stm32::FreeRtosThread commandThread(commandService, "CommandSvc", 2048, 5);

    // ── Central System Controller & Unified CLI Engine ─────────────────────
    static sys::SystemController sysCtrl(NODE_ID,
                                         s_app_image_header,
                                         &netMan,
                                         &tempSensor,
                                         &blinky,
                                         &telemetryService,
                                         &otaService,
                                         &discoveryService);
    sysCtrl.init();

    static sys::CliEngine cliEngine(sysCtrl);
    static sys::ConsoleTask consoleTask(Board_GetDebugUart(), cliEngine);
    static stm32::FreeRtosThread consoleThread(consoleTask, "ConsoleCLI", 1024, 2);

    // Wire dependencies into services
    otaService.set_system_controller(&sysCtrl);
    discoveryService.set_bootloader_version(sysCtrl.get_bootloader_version());
    discoveryService.set_board_id(sysCtrl.get_board_id());
    discoveryService.set_feature_flags(sysCtrl.get_feature_flags());
    commandService.set_system_controller(&sysCtrl);
    commandService.set_cli_engine(&cliEngine);

    blinkThread.start();
    netThread.start();
    discoveryThread.start();
    telemetryThread.start();
    commandThread.start();
    consoleThread.start();

    vTaskStartScheduler();

    while (1) {}
}
