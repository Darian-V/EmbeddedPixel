#pragma once

#include <stdint.h>
#include <stddef.h>

namespace sys {

/**
 * @brief Standard IEEE 802.3 / Ethernet CRC32 calculation utility.
 * Matches Python binascii.crc32(data).
 */
class Crc32 {
public:
    static constexpr uint32_t INITIAL_REMAINDER = 0xFFFFFFFF;
    static constexpr uint32_t FINAL_XOR_VALUE   = 0xFFFFFFFF;

    /**
     * @brief Computes CRC32 over a full buffer in one pass.
     */
    static uint32_t Calculate(const void* data, size_t length);

    /**
     * @brief Updates an ongoing CRC32 calculation with a chunk of data.
     */
    static uint32_t Update(uint32_t currentCrc, const void* data, size_t length);

    /**
     * @brief Finalizes an ongoing CRC32 value.
     */
    static inline uint32_t Finalize(uint32_t currentCrc) {
        return currentCrc ^ FINAL_XOR_VALUE;
    }
};

} // namespace sys
