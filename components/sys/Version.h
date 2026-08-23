#pragma once

#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include "Crc32.h"

namespace sys {

// ── Semantic Versioning Helper ───────────────────────────────────────────────
constexpr uint32_t MAKE_VERSION(uint8_t major, uint8_t minor, uint8_t patch, uint8_t build = 0) {
    return (static_cast<uint32_t>(major) << 24) |
           (static_cast<uint32_t>(minor) << 16) |
           (static_cast<uint32_t>(patch) << 8)  |
           (static_cast<uint32_t>(build));
}

constexpr uint8_t VERSION_MAJOR(uint32_t ver) { return static_cast<uint8_t>((ver >> 24) & 0xFF); }
constexpr uint8_t VERSION_MINOR(uint32_t ver) { return static_cast<uint8_t>((ver >> 16) & 0xFF); }
constexpr uint8_t VERSION_PATCH(uint32_t ver) { return static_cast<uint8_t>((ver >> 8) & 0xFF); }
constexpr uint8_t VERSION_BUILD(uint32_t ver) { return static_cast<uint8_t>(ver & 0xFF); }

inline void format_version(uint32_t ver, char* buf, size_t max_len) {
    uint8_t maj = VERSION_MAJOR(ver);
    uint8_t min = VERSION_MINOR(ver);
    uint8_t pat = VERSION_PATCH(ver);
    uint8_t bld = VERSION_BUILD(ver);
    if (bld > 0) {
        snprintf(buf, max_len, "v%u.%u.%u.%u", maj, min, pat, bld);
    } else {
        snprintf(buf, max_len, "v%u.%u.%u", maj, min, pat);
    }
}

// ── Board Identification ───────────────────────────────────────────────────
enum class BoardId : uint16_t {
    UNKNOWN       = 0x0000,
    NUCLEO_H7S3L8 = 0x0001,
    PIXELJAM_H743 = 0x0002,
};

inline const char* get_board_name(uint16_t board_id) {
    switch (static_cast<BoardId>(board_id)) {
        case BoardId::NUCLEO_H7S3L8: return "Nucleo-H7S3L8";
        case BoardId::PIXELJAM_H743: return "PixelJam-H743";
        default:                     return "Unknown Board";
    }
}

// ── Feature Flags & Capabilities Bitmask ───────────────────────────────────
enum class FeatureFlag : uint32_t {
    NONE                  = 0,
    FEAT_ETHERNET_LAN8742 = (1 << 0),  ///< 100M Ethernet PHY active
    FEAT_TELEMETRY_STREAM = (1 << 1),  ///< Multi-channel UDP telemetry streaming
    FEAT_TEMP_SENSOR_DTS  = (1 << 2),  ///< STM32 On-Chip Digital Temp Sensor
    FEAT_OTA_RAM_STAGING  = (1 << 3),  ///< RAM-staged 2-stage bootloader OTA
    FEAT_OTA_DUAL_BANK    = (1 << 4),  ///< Dual-bank external flash slot switching
    FEAT_COMPRESSION_LZ4  = (1 << 5),  ///< LZ4 compressed OTA payloads
    FEAT_SECURE_BOOT      = (1 << 6),  ///< Cryptographically signed firmware
    FEAT_DYNAMIC_RATE     = (1 << 7),  ///< Runtime dynamic sampling rate configuration
    FEAT_UART_CLI         = (1 << 8),  ///< Interactive UART CLI on COM port
    FEAT_TIME_SYNC        = (1 << 9),  ///< Disciplined local clock & UTC epoch time sync
};

constexpr uint32_t operator|(FeatureFlag a, FeatureFlag b) {
    return static_cast<uint32_t>(a) | static_cast<uint32_t>(b);
}

constexpr uint32_t operator|(uint32_t a, FeatureFlag b) {
    return a | static_cast<uint32_t>(b);
}

inline bool is_feature_set(uint32_t mask, FeatureFlag flag) {
    return (mask & static_cast<uint32_t>(flag)) != 0;
}

inline const char* get_feature_name(FeatureFlag flag) {
    switch (flag) {
        case FeatureFlag::FEAT_ETHERNET_LAN8742: return "ethernet";
        case FeatureFlag::FEAT_TELEMETRY_STREAM: return "telemetry";
        case FeatureFlag::FEAT_TEMP_SENSOR_DTS:  return "dts";
        case FeatureFlag::FEAT_OTA_RAM_STAGING:  return "ota";
        case FeatureFlag::FEAT_OTA_DUAL_BANK:    return "dualbank";
        case FeatureFlag::FEAT_COMPRESSION_LZ4:  return "lz4";
        case FeatureFlag::FEAT_SECURE_BOOT:      return "secboot";
        case FeatureFlag::FEAT_DYNAMIC_RATE:     return "dynrate";
        case FeatureFlag::FEAT_UART_CLI:         return "cli";
        case FeatureFlag::FEAT_TIME_SYNC:        return "timesync";
        default:                                 return "unknown";
    }
}

inline FeatureFlag parse_feature_name(const char* name) {
    if (name == nullptr) return FeatureFlag::NONE;
    if (strcmp(name, "ota") == 0)       return FeatureFlag::FEAT_OTA_RAM_STAGING;
    if (strcmp(name, "telemetry") == 0) return FeatureFlag::FEAT_TELEMETRY_STREAM;
    if (strcmp(name, "dts") == 0)       return FeatureFlag::FEAT_TEMP_SENSOR_DTS;
    if (strcmp(name, "ethernet") == 0)  return FeatureFlag::FEAT_ETHERNET_LAN8742;
    if (strcmp(name, "dynrate") == 0)   return FeatureFlag::FEAT_DYNAMIC_RATE;
    if (strcmp(name, "cli") == 0)       return FeatureFlag::FEAT_UART_CLI;
    if (strcmp(name, "timesync") == 0 || strcmp(name, "time") == 0) return FeatureFlag::FEAT_TIME_SYNC;
    return FeatureFlag::NONE;
}


// ── Application Image Header (Offset 0x200 in Binary) ──────────────────────
#pragma pack(push, 1)

constexpr uint32_t EPFW_MAGIC = 0x45504657; // "EPFW" (EmbeddedPixel FirmWare)
constexpr uint16_t EPFW_HEADER_VERSION = 1;
constexpr uint32_t APP_HEADER_FLASH_OFFSET = 0x200; // 512 bytes offset

struct AppImageHeader {
    uint32_t magic;                  ///< 0x45504657 ("EPFW")
    uint16_t header_version;         ///< Header structure version (1)
    uint16_t board_id;               ///< BoardId enum value
    uint32_t app_version;            ///< Semantic version (e.g. 0x01020000)
    uint32_t min_bootloader_version; ///< Minimum required bootloader version
    uint32_t feature_flags;          ///< FeatureFlag bitmask
    uint32_t image_size;             ///< Total binary size in bytes
    uint32_t image_crc32;            ///< IEEE 802.3 CRC32 over entire binary
    uint32_t build_timestamp;        ///< Unix epoch build timestamp
    uint32_t git_commit;             ///< 32-bit truncated git commit hash
    uint8_t  reserved[20];           ///< Future crypto/signature padding
    uint32_t header_crc32;           ///< CRC32 of first 60 bytes of this struct

    bool isValid() const {
        if (magic != EPFW_MAGIC) return false;
        uint32_t expected = Crc32::Calculate(this, sizeof(AppImageHeader) - sizeof(header_crc32));
        return (header_crc32 == expected);
    }
};

static_assert(sizeof(AppImageHeader) == 64, "AppImageHeader must be exactly 64 bytes");

// ── Bootloader Handover Information ─────────────────────────────────────────
constexpr uint32_t BOOT_INFO_MAGIC = 0x424F4F54; // "BOOT"
constexpr uint32_t BOOT_INFO_RAM_ADDRESS = 0x24070100; // Fixed AXI SRAM handover location

enum class BootReason : uint32_t {
    POWER_ON   = 0,
    SOFT_RESET = 1,
    OTA_UPDATE = 2,
    FAULT      = 3,
    WATCHDOG   = 4,
};

struct BootInfo {
    uint32_t magic;                  ///< 0x424F4F54 ("BOOT")
    uint32_t bootloader_version;     ///< Semantic version of active bootloader
    uint32_t boot_reason;            ///< BootReason enum value
    uint32_t active_slot;            ///< 0 = Slot A (0x70000000), 1 = Slot B
    uint32_t boot_count;             ///< Incremented on each cold/warm boot
    uint32_t reserved[2];
    uint32_t struct_crc32;           ///< CRC32 of first 28 bytes

    bool isValid() const {
        if (magic != BOOT_INFO_MAGIC) return false;
        uint32_t expected = Crc32::Calculate(this, sizeof(BootInfo) - sizeof(struct_crc32));
        return (struct_crc32 == expected);
    }
};

static_assert(sizeof(BootInfo) == 32, "BootInfo must be exactly 32 bytes");

#pragma pack(pop)

} // namespace sys
