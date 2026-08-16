#pragma once

#include "ProtocolTypes.h"
#include <string.h>

namespace net::proto {

class PacketHelper {
public:
    /**
     * @brief Populates a standard PE_Header structure.
     */
    static void PopulateHeader(PE_Header& hdr,
                               uint16_t node_id,
                               MessageType type,
                               uint32_t seq_num,
                               uint16_t payload_len,
                               uint8_t flags = 0,
                               bool compute_crc = false,
                               const void* payload = nullptr) {
        hdr.magic       = PE_MAGIC;
        hdr.version     = PE_PROTOCOL_VERSION;
        hdr.flags       = flags;
        hdr.node_id     = node_id;
        hdr.msg_type    = static_cast<uint16_t>(type);
        hdr.seq_num     = seq_num;
        hdr.payload_len = payload_len;

        if (compute_crc && payload != nullptr && payload_len > 0) {
            hdr.crc16 = crc16_ccitt(payload, payload_len);
        } else {
            hdr.crc16 = 0;
        }
    }

    /**
     * @brief Validates packet header integrity.
     */
    static StatusCode ValidateHeader(const PE_Header& hdr, size_t received_bytes) {
        if (received_bytes < sizeof(PE_Header)) {
            return StatusCode::ERR_INVALID_PAYLOAD;
        }
        if (hdr.magic != PE_MAGIC) {
            return StatusCode::ERR_INVALID_MAGIC;
        }
        if (hdr.version != PE_PROTOCOL_VERSION) {
            return StatusCode::ERR_INVALID_VERSION;
        }
        if (received_bytes < (sizeof(PE_Header) + hdr.payload_len)) {
            return StatusCode::ERR_INVALID_PAYLOAD;
        }
        return StatusCode::OK;
    }

    /**
     * @brief Validates payload CRC16 if present.
     */
    static bool ValidatePayloadCrc(const PE_Header& hdr, const void* payload) {
        if (hdr.crc16 == 0 || hdr.payload_len == 0 || payload == nullptr) {
            return true; // No CRC checked
        }
        return (crc16_ccitt(payload, hdr.payload_len) == hdr.crc16);
    }
};

} // namespace net::proto
