#pragma once

#include "Version.h"
#include "proto/ProtocolTypes.h"
#include <stdint.h>
#include <stddef.h>

namespace hal {
class ITempSensor;
}

namespace app {
class BlinkTask;
}

namespace net {
class NetManager;
namespace services {
class TelemetryService;
class OtaService;
class DiscoveryService;
}
}

namespace sys {

class SystemController {
public:
    SystemController(uint16_t nodeId,
                     const AppImageHeader& appHeader,
                     net::NetManager* netMan = nullptr,
                     hal::ITempSensor* tempSensor = nullptr,
                     app::BlinkTask* blinkTask = nullptr,
                     net::services::TelemetryService* telemSvc = nullptr,
                     net::services::OtaService* otaSvc = nullptr,
                     net::services::DiscoveryService* discSvc = nullptr);

    void init();

    // Feature Flags Management
    bool is_feature_enabled(FeatureFlag flag) const;
    void set_feature(FeatureFlag flag, bool enable);
    uint32_t get_feature_flags() const { return feature_flags_; }

    // Versioning & Identification
    uint32_t get_app_version() const { return app_header_.app_version; }
    uint32_t get_bootloader_version() const { return boot_info_.bootloader_version; }
    uint16_t get_board_id() const { return app_header_.board_id; }
    const char* get_board_name() const { return sys::get_board_name(app_header_.board_id); }
    uint32_t get_git_commit() const { return app_header_.git_commit; }
    uint32_t get_build_timestamp() const { return app_header_.build_timestamp; }
    uint16_t get_node_id() const { return node_id_; }

    // Diagnostics & State
    uint32_t get_uptime_ms() const;
    float get_core_temp_c() const;
    uint32_t get_ip_addr() const;
    const uint8_t* get_mac_addr() const;
    const BootInfo& get_boot_info() const { return boot_info_; }

    // Subsystem Control
    void set_blink_rate(uint32_t periodMs);
    uint32_t get_blink_rate() const;

    bool start_telemetry(uint32_t tag = 0, uint16_t rate = 0, uint16_t batch = 0);
    bool stop_telemetry(uint32_t tag = 0);
    bool is_streaming() const;

    void abort_ota();

    // System Control
    void reboot();

    // Dependency Registration (if initialized later)
    void set_net_manager(net::NetManager* netMan) { net_ = netMan; }
    void set_temp_sensor(hal::ITempSensor* sensor) { temp_sensor_ = sensor; }
    void set_blink_task(app::BlinkTask* task) { blink_ = task; }
    void set_telemetry_service(net::services::TelemetryService* svc) { telemetry_ = svc; }
    void set_ota_service(net::services::OtaService* svc) { ota_ = svc; }
    void set_discovery_service(net::services::DiscoveryService* svc) { discovery_ = svc; }

private:
    uint16_t                         node_id_;
    AppImageHeader                   app_header_;
    BootInfo                         boot_info_;
    uint32_t                         feature_flags_;
    net::NetManager*                 net_;
    hal::ITempSensor*                temp_sensor_;
    app::BlinkTask*                  blink_;
    net::services::TelemetryService* telemetry_;
    net::services::OtaService*       ota_;
    net::services::DiscoveryService* discovery_;
};

} // namespace sys
