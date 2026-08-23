#include "SystemController.h"
#include "ITempSensor.h"
#include "BlinkTask.h"
#include "NetManager.h"
#include "TelemetryService.h"
#include "OtaService.h"
#include "DiscoveryService.h"
#include "proto/ProtocolTypes.h"

// FreeRTOS & HAL
#include "FreeRTOS.h"
#include "task.h"
#include "stm32h7rsxx_hal.h"

namespace sys {
using namespace net::proto;

SystemController::SystemController(uint16_t nodeId,
                                   const AppImageHeader& appHeader,
                                   net::NetManager* netMan,
                                   hal::ITempSensor* tempSensor,
                                   app::BlinkTask* blinkTask,
                                   net::services::TelemetryService* telemSvc,
                                   net::services::OtaService* otaSvc,
                                   net::services::DiscoveryService* discSvc)
    : node_id_(nodeId),
      app_header_(appHeader),
      boot_info_{},
      feature_flags_(appHeader.feature_flags),
      net_(netMan),
      temp_sensor_(tempSensor),
      blink_(blinkTask),
      telemetry_(telemSvc),
      ota_(otaSvc),
      discovery_(discSvc) {
}

void SystemController::init() {
    // 1. Read Bootloader Handover Info from designated SRAM address (0x24070100)
    SCB_InvalidateDCache_by_Addr(reinterpret_cast<uint32_t*>(BOOT_INFO_RAM_ADDRESS), sizeof(BootInfo));
    const auto* ram_boot_info = reinterpret_cast<const BootInfo*>(BOOT_INFO_RAM_ADDRESS);

    if (ram_boot_info->isValid()) {
        boot_info_ = *ram_boot_info;
    } else {
        // Default fallback if booted without explicit bootloader handover block
        boot_info_.magic              = BOOT_INFO_MAGIC;
        boot_info_.bootloader_version = MAKE_VERSION(1, 0, 0, 0);
        boot_info_.boot_reason        = static_cast<uint32_t>(BootReason::POWER_ON);
        boot_info_.active_slot        = 0;
        boot_info_.boot_count         = 1;
        boot_info_.struct_crc32       = Crc32::Calculate(&boot_info_, sizeof(BootInfo) - sizeof(boot_info_.struct_crc32));
    }
}

bool SystemController::is_feature_enabled(FeatureFlag flag) const {
    return (feature_flags_ & static_cast<uint32_t>(flag)) != 0;
}

void SystemController::set_feature(FeatureFlag flag, bool enable) {
    if (enable) {
        feature_flags_ |= static_cast<uint32_t>(flag);
    } else {
        feature_flags_ &= ~static_cast<uint32_t>(flag);
    }
}

uint32_t SystemController::get_uptime_ms() const {
    return xTaskGetTickCount() * portTICK_PERIOD_MS;
}

float SystemController::get_core_temp_c() const {
    if (temp_sensor_ != nullptr) {
        int32_t t_c = 0;
        if (temp_sensor_->get_temperature(t_c)) {
            return static_cast<float>(t_c);
        }
    }
    return 0.0f;
}

uint32_t SystemController::get_ip_addr() const {
    if (net_ != nullptr) {
        return net_->get_ip_addr();
    }
    return 0;
}

const uint8_t* SystemController::get_mac_addr() const {
    if (net_ != nullptr) {
        return net_->get_mac_addr();
    }
    return nullptr;
}

void SystemController::set_blink_rate(uint32_t periodMs) {
    if (blink_ != nullptr) {
        blink_->set_period_ms(periodMs);
    }
}

uint32_t SystemController::get_blink_rate() const {
    if (blink_ != nullptr) {
        return blink_->get_period_ms();
    }
    return 0;
}

bool SystemController::start_telemetry(uint32_t tag, uint16_t rate, uint16_t batch) {
    if (!is_feature_enabled(FeatureFlag::FEAT_TELEMETRY_STREAM) || telemetry_ == nullptr) {
        return false;
    }
    ip_addr_t bcast;
    ip_addr_set_ip4_u32(&bcast, IPADDR_BROADCAST);
    telemetry_->start_streaming(bcast, net::proto::PORT_STREAM, tag, rate, batch);
    if (discovery_ != nullptr) {
        discovery_->set_state(net::proto::NodeState::STREAMING);
    }
    return true;
}

bool SystemController::stop_telemetry(uint32_t tag) {
    if (telemetry_ == nullptr) return false;
    telemetry_->stop_streaming(tag);
    if (!telemetry_->is_streaming() && discovery_ != nullptr) {
        discovery_->set_state(net::proto::NodeState::IDLE);
    }
    return true;
}

bool SystemController::is_streaming() const {
    return (telemetry_ != nullptr && telemetry_->is_streaming());
}

void SystemController::abort_ota() {
    if (ota_ != nullptr) {
        ota_->handleAbort();
    }
}

void SystemController::reboot() {
    vTaskDelay(pdMS_TO_TICKS(100));
    __DSB();
    NVIC_SystemReset();
    while (1) { __NOP(); }
}

} // namespace sys
