#include "OtaService.h"
#include "net_log.h"
#include "stm32h7rsxx_hal.h"
#include <string.h>

namespace net::services {

OtaService::OtaService(hal::IFlashDriver& flashDriver)
    : flash_(flashDriver),
      state_(InternalState::IDLE),
      last_error_(proto::StatusCode::OK),
      total_image_size_(0),
      expected_crc32_(0),
      target_version_(0),
      bytes_written_(0),
      incremental_crc_(sys::Crc32::INITIAL_REMAINDER),
      auto_reboot_(false),
      reboot_pending_(false) {}

proto::StatusCode OtaService::handleBegin(const proto::PayloadOtaBegin& req,
                                          proto::PayloadOtaBeginResp& resp) {
    LOG_INFO("OTA: Begin requested (size=%lu B, crc=0x%08lX, ver=0x%08lX)\r\n",
             req.image_size, req.image_crc32, req.target_version);

    if (req.image_size == 0 || req.image_size > MAX_IMAGE_SIZE) {
        last_error_ = proto::StatusCode::ERR_IMAGE_TOO_LARGE;
        resp.status_code = static_cast<uint32_t>(last_error_);
        return last_error_;
    }

    total_image_size_ = req.image_size;
    expected_crc32_   = req.image_crc32;
    target_version_   = req.target_version;
    bytes_written_    = 0;
    incremental_crc_  = sys::Crc32::INITIAL_REMAINDER;
    auto_reboot_      = (req.flags & 0x01) != 0;
    reboot_pending_   = false;
    last_error_       = proto::StatusCode::OK;

    // Reset RAM staging buffer
    memset(staging_buffer_, 0xFF, (total_image_size_ + 255) & ~255);

    state_ = InternalState::RECEIVING;
    resp.status_code       = static_cast<uint32_t>(proto::StatusCode::OK);
    resp.chunk_size_ack    = 1024;
    resp.max_image_size_kb = MAX_IMAGE_SIZE / 1024;

    LOG_INFO("OTA: Staging RAM ready (%p). Ready to receive chunks.\r\n", static_cast<void*>(staging_buffer_));
    return proto::StatusCode::OK;
}

proto::StatusCode OtaService::handleData(const proto::PayloadOtaData& req,
                                         const uint8_t* chunkData,
                                         uint16_t dataLen) {
    if (state_ != InternalState::RECEIVING) {
        return proto::StatusCode::ERR_BUSY;
    }

    if (req.offset + dataLen > total_image_size_) {
        LOG_ERR("OTA: Payload exceeds image size (offset=%lu, len=%u, total=%lu)\r\n",
                req.offset, dataLen, total_image_size_);
        last_error_ = proto::StatusCode::ERR_INVALID_PAYLOAD;
        return last_error_;
    }

    // Validate chunk CRC16
    if (req.chunk_crc16 != 0) {
        uint16_t calc_crc16 = proto::crc16_ccitt(chunkData, dataLen);
        if (calc_crc16 != req.chunk_crc16) {
            LOG_ERR("OTA: Chunk CRC16 mismatch at offset %lu (got 0x%04X, expected 0x%04X)\r\n",
                    req.offset, calc_crc16, req.chunk_crc16);
            last_error_ = proto::StatusCode::ERR_INVALID_CRC;
            return last_error_;
        }
    }

    // Write directly into internal RAM staging buffer
    memcpy(&staging_buffer_[req.offset], chunkData, dataLen);

    // Update incremental CRC32 in parallel
    incremental_crc_ = sys::Crc32::Update(incremental_crc_, chunkData, dataLen);

    bytes_written_ = req.offset + dataLen;
    return proto::StatusCode::OK;
}

proto::StatusCode OtaService::handleEnd(const proto::PayloadOtaEnd& req) {
    if (state_ != InternalState::RECEIVING) {
        return proto::StatusCode::ERR_BUSY;
    }

    LOG_INFO("OTA: End received. Verifying RAM staging CRC32...\r\n");
    state_ = InternalState::VERIFYING;

    // Clean D-Cache to ensure all written bytes are pushed from L1 cache to RAM
    SCB_CleanDCache_by_Addr((uint32_t*)staging_buffer_, total_image_size_);

    // Verify CRC32 across all received bytes directly in RAM
    uint32_t buffer_crc32 = sys::Crc32::Calculate(staging_buffer_, total_image_size_);
    uint32_t stream_crc32 = sys::Crc32::Finalize(incremental_crc_);

    LOG_INFO("OTA: Staging header bytes: %02X %02X %02X %02X %02X %02X %02X %02X\r\n",
             staging_buffer_[0], staging_buffer_[1], staging_buffer_[2], staging_buffer_[3],
             staging_buffer_[4], staging_buffer_[5], staging_buffer_[6], staging_buffer_[7]);
    LOG_INFO("OTA: Stream CRC32=0x%08lX, Buffer CRC32=0x%08lX, Expected=0x%08lX (Bytes=%lu)\r\n",
             stream_crc32, buffer_crc32, expected_crc32_, bytes_written_);

    if (buffer_crc32 != expected_crc32_) {
        LOG_ERR("OTA: CRC32 verification FAILED! (calculated 0x%08lX, expected 0x%08lX)\r\n",
                buffer_crc32, expected_crc32_);
        state_ = InternalState::ERROR_STATE;
        last_error_ = proto::StatusCode::ERR_INVALID_CRC;
        return last_error_;
    }

    LOG_INFO("OTA: CRC32 verified successfully: 0x%08lX\r\n", buffer_crc32);

    // Write persistent control block in RAM for bootloader
    if (!writeControlBlock(proto::OtaState::PENDING_INSTALL)) {
        LOG_ERR("OTA: Failed to write OTA Control Block!\r\n");
        state_ = InternalState::ERROR_STATE;
        last_error_ = proto::StatusCode::ERR_INTERNAL;
        return last_error_;
    }

    state_ = InternalState::READY;
    if (auto_reboot_ || req.auto_reboot != 0) {
        reboot_pending_ = true;
    }

    LOG_INFO("OTA: Firmware staged in RAM and marked PENDING_INSTALL. Ready to reboot.\r\n");
    return proto::StatusCode::OK;
}

proto::StatusCode OtaService::handleAbort() {
    LOG_INFO("OTA: Update aborted by host.\r\n");
    state_ = InternalState::IDLE;
    last_error_ = proto::StatusCode::OK;
    bytes_written_ = 0;
    reboot_pending_ = false;
    return proto::StatusCode::OK;
}

void OtaService::getStatus(proto::PayloadOtaStatusResp& resp) const {
    resp.bytes_written = bytes_written_;
    resp.total_bytes   = total_image_size_;
    resp.state         = static_cast<uint16_t>(state_);
    resp.error_code    = static_cast<uint16_t>(last_error_);
}

bool OtaService::writeControlBlock(proto::OtaState otaState) {
    proto::OtaControlBlock blk = {0};
    blk.magic           = proto::OTA_MAGIC;
    blk.state           = static_cast<uint32_t>(otaState);
    blk.image_size      = total_image_size_;
    blk.image_crc32     = expected_crc32_;
    blk.target_version  = target_version_;
    blk.staging_address = reinterpret_cast<uint32_t>(staging_buffer_);
    blk.active_address  = SLOT_A_PHYSICAL_BASE;
    blk.struct_crc32    = sys::Crc32::Calculate(&blk, sizeof(blk) - sizeof(blk.struct_crc32));

    // Store in designated RAM control block location (in AXI SRAM at 0x24070000)
    proto::OtaControlBlock* ram_blk = reinterpret_cast<proto::OtaControlBlock*>(RAM_CONTROL_BLOCK_BASE);
    *ram_blk = blk;

    // Clean D-Cache so physical SRAM is written before reset
    SCB_CleanDCache_by_Addr(reinterpret_cast<uint32_t*>(staging_buffer_), total_image_size_);
    SCB_CleanDCache_by_Addr(reinterpret_cast<uint32_t*>(RAM_CONTROL_BLOCK_BASE), sizeof(proto::OtaControlBlock));
    __DSB();
    __ISB();

    return true;
}

} // namespace net::services
