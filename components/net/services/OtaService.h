#pragma once

#include "IFlashDriver.h"
#include "proto/ProtocolTypes.h"
#include "Crc32.h"
#include "Version.h"
#include <stdint.h>
#include <stddef.h>

namespace sys {
class SystemController;
}

namespace net::services {

/**
 * @brief Manages Ethernet OTA firmware updates, chunk validation, RAM staging, and reboot control.
 */
class OtaService {
public:
    static constexpr uint32_t SLOT_A_PHYSICAL_BASE    = 0x70000000;
    static constexpr uint32_t RAM_CONTROL_BLOCK_BASE  = 0x24070000; // Top of AXI SRAM (safe from DTCM stack growth)
    static constexpr uint32_t MAX_IMAGE_SIZE          = 160 * 1024; // 160 KB max image

    enum class InternalState : uint16_t {
        IDLE        = 0,
        RECEIVING   = 1,
        VERIFYING   = 2,
        READY       = 3,
        ERROR_STATE = 4,
    };

    explicit OtaService(hal::IFlashDriver& flashDriver, sys::SystemController* sysCtrl = nullptr);
    ~OtaService() = default;

    void set_system_controller(sys::SystemController* sysCtrl) { sys_ctrl_ = sysCtrl; }

    proto::StatusCode handleBegin(const proto::PayloadOtaBegin& req, proto::PayloadOtaBeginResp& resp);
    proto::StatusCode handleData(const proto::PayloadOtaData& req, const uint8_t* chunkData, uint16_t dataLen);
    proto::StatusCode handleEnd(const proto::PayloadOtaEnd& req);
    proto::StatusCode handleAbort();

    void getStatus(proto::PayloadOtaStatusResp& resp) const;
    bool is_reboot_pending() const { return reboot_pending_; }

private:
    hal::IFlashDriver&     flash_;
    sys::SystemController* sys_ctrl_;
    InternalState          state_;
    proto::StatusCode      last_error_;
    uint32_t               total_image_size_;
    uint32_t               expected_crc32_;
    uint32_t               target_version_;
    uint32_t               bytes_written_;
    uint32_t               incremental_crc_;
    bool                   auto_reboot_;
    bool                   reboot_pending_;

    alignas(32) uint8_t staging_buffer_[MAX_IMAGE_SIZE];

    bool writeControlBlock(proto::OtaState otaState);
};

} // namespace net::services
