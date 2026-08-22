#pragma once

#include <stdint.h>
#include <stddef.h>

namespace net::proto {

// ── Protocol Port Constants ────────────────────────────────────────────────
constexpr uint16_t PORT_DISCOVERY = 50000;
constexpr uint16_t PORT_STREAM    = 50001;
constexpr uint16_t PORT_COMMAND   = 50002;

// ── Protocol Constants ─────────────────────────────────────────────────────
constexpr uint16_t PE_MAGIC            = 0x5045;   ///< 'P', 'E'
constexpr uint8_t  PE_PROTOCOL_VERSION = 1;

// ── Protocol Flags ─────────────────────────────────────────────────────────
constexpr uint8_t FLAG_ACK_REQUESTED = (1 << 0);
constexpr uint8_t FLAG_IS_RESPONSE   = (1 << 1);
constexpr uint8_t FLAG_ERROR         = (1 << 2);
constexpr uint8_t FLAG_HAS_TIMESTAMP = (1 << 3);

// ── Message Types ──────────────────────────────────────────────────────────
enum class MessageType : uint16_t {
    // Discovery & System
    HEARTBEAT               = 0x0001,
    DISCOVERY_PING          = 0x0002,
    DISCOVERY_PONG          = 0x0003,
    TIME_SYNC_REQ           = 0x0010,
    TIME_SYNC_RESP          = 0x0011,

    // Control & RPC
    CMD_GET_NODE_INFO       = 0x0100,
    CMD_GET_NODE_INFO_RESP  = 0x0101,
    CMD_SET_CONFIG          = 0x0102,
    CMD_GET_CONFIG          = 0x0103,
    CMD_GET_CONFIG_RESP     = 0x0104,
    CMD_START_STREAM        = 0x0110,
    CMD_STOP_STREAM         = 0x0111,
    CMD_GET_STREAMS         = 0x0120,
    CMD_GET_STREAMS_RESP    = 0x0121,

    // OTA Firmware Updates
    CMD_OTA_BEGIN           = 0x0130,
    CMD_OTA_BEGIN_RESP      = 0x0131,
    CMD_OTA_DATA            = 0x0132,
    CMD_OTA_DATA_RESP       = 0x0133,
    CMD_OTA_END             = 0x0134,
    CMD_OTA_END_RESP        = 0x0135,
    CMD_OTA_GET_STATUS      = 0x0136,
    CMD_OTA_GET_STATUS_RESP = 0x0137,
    CMD_OTA_ABORT           = 0x0138,

    // Unified CLI Command Execution (Text commands over TCP/UART)
    CMD_CLI_EXEC            = 0x0150,
    CMD_CLI_EXEC_RESP       = 0x0151,

    CMD_REBOOT              = 0x01F0,
    CMD_ACK                 = 0x01FE,
    CMD_NACK                = 0x01FF,

    // Streaming & Telemetry
    STREAM_SENSOR_BATCH     = 0x0200,
    STREAM_STATUS_TELEMETRY = 0x0201,
    STREAM_EVENT_ALERT      = 0x0202,
};

// ── Status Codes ───────────────────────────────────────────────────────────
enum class StatusCode : uint16_t {
    OK                          = 0x0000,
    ERR_INVALID_MAGIC           = 0x0001,
    ERR_INVALID_VERSION         = 0x0002,
    ERR_INVALID_CRC             = 0x0003,
    ERR_UNKNOWN_CMD             = 0x0004,
    ERR_INVALID_PAYLOAD         = 0x0005,
    ERR_BUSY                    = 0x0006,
    ERR_FLASH_WRITE             = 0x0007,
    ERR_FLASH_ERASE             = 0x0008,
    ERR_IMAGE_TOO_LARGE         = 0x0009,
    ERR_OTA_DISABLED            = 0x000A,
    ERR_INCOMPATIBLE_BOARD      = 0x000B,
    ERR_INCOMPATIBLE_BOOTLOADER = 0x000C,
    ERR_VERSION_DOWNGRADE       = 0x000D,
    ERR_INTERNAL                = 0x00FF,
};

// ── Node State ─────────────────────────────────────────────────────────────
enum class NodeState : uint8_t {
    INIT      = 0,
    IDLE      = 1,
    STREAMING = 2,
    FAULT     = 3,
};

// ── Stream Sample Types ────────────────────────────────────────────────────
enum class SampleType : uint16_t {
    INT16   = 0,
    UINT16  = 1,
    INT32   = 2,
    FLOAT32 = 3,
    UINT32  = 4,
};

// ── Packed Structures (1-byte alignment) ───────────────────────────────────
#pragma pack(push, 1)

/**
 * @brief Common 16-byte protocol header present in all packets.
 */
struct PE_Header {
    uint16_t magic;         ///< 0x5045 ('P', 'E')
    uint8_t  version;       ///< Protocol version (0x01)
    uint8_t  flags;         ///< Protocol flags
    uint16_t node_id;       ///< Originating Node ID (1..65535)
    uint16_t msg_type;      ///< MessageType enum value
    uint32_t seq_num;       ///< Monotonic sequence number
    uint16_t payload_len;   ///< Length of payload following header
    uint16_t crc16;         ///< CRC16-CCITT of payload (or 0 if disabled)
};

static_assert(sizeof(PE_Header) == 16, "PE_Header must be exactly 16 bytes");

/**
 * @brief Payload for 1 Hz node heartbeat broadcast.
 */
struct PayloadHeartbeat {
    uint32_t uptime_ms;         ///< Time since boot in ms
    uint32_t fw_version;        ///< Firmware version
    uint8_t  node_state;        ///< NodeState enum value
    uint8_t  active_streams;    ///< Active stream bitmask
    uint16_t vdd_mv;            ///< Supply voltage in mV
    int16_t  core_temp_c_x10;   ///< Core junction temperature (0.1 C)
    uint16_t feature_flags_low; ///< Lower 16 bits of FeatureFlag bitmask
};

static_assert(sizeof(PayloadHeartbeat) == 16, "PayloadHeartbeat must be exactly 16 bytes");

/**
 * @brief Payload for host discovery probe.
 */
struct PayloadDiscoveryPing {
    uint32_t challenge_id;      ///< Correlation ID from host
    uint16_t target_node_id;    ///< Target Node ID (0 = all nodes)
    uint16_t reserved;
};

/**
 * @brief Payload for node discovery response.
 */
struct PayloadDiscoveryPong {
    uint32_t challenge_id;       ///< Echoed challenge ID
    uint16_t node_id;            ///< Node ID
    uint16_t node_state;         ///< NodeState enum value
    uint32_t ip_addr;            ///< Current IP (Network order)
    uint8_t  mac_addr[6];        ///< MAC address
    uint16_t board_id;           ///< Board ID (e.g., 0x0001 for Nucleo-H7S3L8)
    uint32_t fw_version;         ///< Active Application Firmware version
    uint32_t uptime_ms;          ///< Uptime in ms
    uint32_t hw_uid[3];          ///< 96-bit STM32 Hardware UID
    uint32_t bootloader_version; ///< Active Bootloader version
    uint32_t feature_flags;      ///< Active FeatureFlag bitmask
};

static_assert(sizeof(PayloadDiscoveryPong) == 48, "PayloadDiscoveryPong must be exactly 48 bytes");

/**
 * @brief Payload for executing text CLI commands over TCP.
 */
struct PayloadCliExec {
    uint16_t cmd_len;           ///< Length of ASCII command text
    uint16_t flags;             ///< Reserved
};

/**
 * @brief Response payload for CLI command execution.
 */
struct PayloadCliExecResp {
    uint16_t status_code;       ///< StatusCode value
    uint16_t resp_len;          ///< Length of ASCII response text
};

/**
 * @brief Payload for command RPC execution.
 */
struct PayloadCommand {
    uint16_t cmd_id;            ///< Sub-command ID
    uint16_t param1;            ///< Parameter 1
    uint32_t param2;            ///< Parameter 2
};

/**
 * @brief Payload for command acknowledgement / error.
 */
struct PayloadAckNack {
    uint16_t cmd_id;            ///< Echoed command ID
    uint16_t status_code;       ///< StatusCode enum value
    uint32_t result_data;       ///< Return value or error detail
    uint32_t reserved;
};

/**
 * @brief Payload for initiating OTA firmware transfer.
 */
struct PayloadOtaBegin {
    uint32_t image_size;        ///< Total firmware size in bytes
    uint32_t image_crc32;       ///< Expected CRC32 (IEEE 802.3)
    uint32_t target_version;    ///< Target version (e.g. 0x00010100)
    uint16_t chunk_size;        ///< Preferred chunk size (e.g. 1024)
    uint16_t flags;             ///< Bit 0: auto-reboot
};

static_assert(sizeof(PayloadOtaBegin) == 16, "PayloadOtaBegin must be exactly 16 bytes");

/**
 * @brief Response payload for CMD_OTA_BEGIN_RESP.
 */
struct PayloadOtaBeginResp {
    uint32_t status_code;       ///< StatusCode value
    uint16_t chunk_size_ack;    ///< Accepted chunk size
    uint16_t max_image_size_kb; ///< Maximum supported image size (KB)
};

static_assert(sizeof(PayloadOtaBeginResp) == 8, "PayloadOtaBeginResp must be exactly 8 bytes");

/**
 * @brief Payload header for OTA data chunks.
 */
struct PayloadOtaData {
    uint32_t offset;            ///< Offset in bytes from start of image
    uint16_t chunk_len;         ///< Number of payload data bytes following this struct
    uint16_t chunk_crc16;       ///< CRC16 of chunk payload
};

static_assert(sizeof(PayloadOtaData) == 8, "PayloadOtaData must be exactly 8 bytes");

/**
 * @brief Payload for finalizing OTA transfer.
 */
struct PayloadOtaEnd {
    uint32_t image_crc32;       ///< Final full image CRC32
    uint8_t  auto_reboot;       ///< 1 = reboot immediately upon success
    uint8_t  reserved[3];
};

static_assert(sizeof(PayloadOtaEnd) == 8, "PayloadOtaEnd must be exactly 8 bytes");

/**
 * @brief Payload for OTA status query response.
 */
struct PayloadOtaStatusResp {
    uint32_t bytes_written;     ///< Bytes successfully written
    uint32_t total_bytes;       ///< Total image size
    uint16_t state;             ///< 0=IDLE, 1=RECEIVING, 2=VERIFYING, 3=READY, 4=ERROR
    uint16_t error_code;        ///< StatusCode value
};

static_assert(sizeof(PayloadOtaStatusResp) == 12, "PayloadOtaStatusResp must be exactly 12 bytes");

/**
 * @brief Persistent OTA Control Block located in BKPSRAM (0x38800000) or Flash metadata.
 */
constexpr uint32_t OTA_MAGIC = 0x4F544131; // "OTA1"
constexpr uint32_t OTA_SLOT_A_ADDRESS = 0x70000000;
constexpr uint32_t OTA_SLOT_B_ADDRESS = 0x70800000;
constexpr uint32_t OTA_BKPSRAM_ADDRESS = 0x38800000;

enum class OtaState : uint32_t {
    IDLE            = 0,
    PENDING_INSTALL = 1,
    ACTIVE_SUCCESS  = 2,
    FAILED          = 3,
};

struct OtaControlBlock {
    uint32_t magic;             ///< 0x4F544131 ("OTA1")
    uint32_t state;             ///< OtaState enum value
    uint32_t image_size;        ///< Total size in bytes
    uint32_t image_crc32;       ///< Verified CRC32
    uint32_t target_version;    ///< Target version (0x00010100)
    uint32_t staging_address;   ///< Flash base address of Slot B (0x70800000)
    uint32_t active_address;    ///< Flash base address of Slot A (0x70000000)
    uint32_t struct_crc32;      ///< CRC32 of first 7 fields
};

static_assert(sizeof(OtaControlBlock) == 32, "OtaControlBlock must be exactly 32 bytes");

// ── FourCC Channel Tags ───────────────────────────────────────────────────
constexpr uint32_t MAKE_FOURCC(char a, char b, char c, char d) {
    return (static_cast<uint32_t>(static_cast<uint8_t>(a))) |
           (static_cast<uint32_t>(static_cast<uint8_t>(b)) << 8) |
           (static_cast<uint32_t>(static_cast<uint8_t>(c)) << 16) |
           (static_cast<uint32_t>(static_cast<uint8_t>(d)) << 24);
}

constexpr uint32_t STREAM_TAG_COUNTER = MAKE_FOURCC('C', 'N', 'T', 'R'); ///< "CNTR"
constexpr uint32_t STREAM_TAG_TEMP    = MAKE_FOURCC('T', 'E', 'M', 'P'); ///< "TEMP"
constexpr uint32_t STREAM_TAG_ADC     = MAKE_FOURCC('A', 'D', 'C', '0'); ///< "ADC0"
constexpr uint32_t STREAM_TAG_IMU     = MAKE_FOURCC('I', 'M', 'U', '0'); ///< "IMU0"
constexpr uint32_t STREAM_TAG_PIXELS  = MAKE_FOURCC('P', 'I', 'X', 'L'); ///< "PIXL"

/**
 * @brief Header preceding raw sample array in high-speed streaming packets.
 */
struct StreamPayloadHeader {
    uint64_t timestamp_us;      ///< Hardware timer timestamp of first sample (microsecond)
    uint32_t stream_tag;        ///< 4-character FourCC identifier (e.g. 'CNTR', 'ADC0')
    uint16_t sample_rate_hz;    ///< Sampling frequency (Hz)
    uint16_t sample_count;      ///< Samples packed in this frame
    uint16_t channel_count;     ///< Number of channels per sample
    uint16_t sample_type;       ///< SampleType enum value
};

/**
 * @brief Descriptor for a single registered channel stream.
 */
struct StreamDescriptor {
    uint32_t stream_tag;        ///< 4-character FourCC identifier (e.g. 'CNTR')
    char     name[16];          ///< Null-terminated stream name (e.g. "Counter")
    uint16_t sample_rate_hz;    ///< Native/configured sampling rate (Hz)
    uint16_t batch_count;       ///< Batch count
    uint16_t channel_count;     ///< Number of channels per sample
    uint16_t sample_type;       ///< SampleType enum value
    uint8_t  is_enabled;        ///< 1 = active, 0 = disabled
    uint8_t  reserved[3];       ///< Alignment padding
};

static_assert(sizeof(StreamDescriptor) == 32, "StreamDescriptor must be exactly 32 bytes");

/**
 * @brief Response payload for CMD_GET_STREAMS.
 */
struct PayloadGetStreamsResp {
    uint16_t stream_count;      ///< Number of following StreamDescriptor entries
    uint16_t reserved;
};

static_assert(sizeof(PayloadGetStreamsResp) == 4, "PayloadGetStreamsResp must be exactly 4 bytes");

#pragma pack(pop)

// ── CRC16-CCITT Calculation ───────────────────────────────────────────────
inline uint16_t crc16_ccitt(const void* data, size_t length, uint16_t seed = 0xFFFF) {
    const uint8_t* p = static_cast<const uint8_t*>(data);
    uint16_t crc = seed;
    for (size_t i = 0; i < length; ++i) {
        crc ^= (static_cast<uint16_t>(p[i]) << 8);
        for (int b = 0; b < 8; ++b) {
            if (crc & 0x8000) {
                crc = (crc << 1) ^ 0x1021;
            } else {
                crc = crc << 1;
            }
        }
    }
    return crc;
}

} // namespace net::proto
