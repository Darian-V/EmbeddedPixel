#pragma once

#include <stdint.h>
#include <stddef.h>

namespace hal {

/**
 * @brief Abstract hardware interface for Non-Volatile Flash memory (internal or external NOR/XSPI).
 */
class IFlashDriver {
public:
    virtual ~IFlashDriver() = default;

    /**
     * @brief Initializes the flash hardware/controller.
     * @return true on success, false on failure.
     */
    virtual bool init() = 0;

    /**
     * @brief Erases flash sectors covering the specified offset and length.
     * @param offset Byte offset from flash base (e.g. 0x00800000 for Slot B)
     * @param size Number of bytes to erase
     * @return true on success, false on failure.
     */
    virtual bool erase(uint32_t offset, size_t size) = 0;

    /**
     * @brief Programs data to flash at the specified byte offset.
     * @param offset Byte offset from flash base
     * @param data Pointer to source data buffer
     * @param size Number of bytes to write
     * @return true on success, false on failure.
     */
    virtual bool write(uint32_t offset, const void* data, size_t size) = 0;

    /**
     * @brief Reads data from flash.
     * @param offset Byte offset from flash base
     * @param buffer Destination buffer
     * @param size Number of bytes to read
     * @return true on success, false on failure.
     */
    virtual bool read(uint32_t offset, void* buffer, size_t size) = 0;

    /**
     * @brief Returns sector erase granularity in bytes (e.g. 4096 or 65536).
     */
    virtual size_t get_sector_size() const = 0;

    /**
     * @brief Returns page programming size in bytes (e.g. 256).
     */
    virtual size_t get_page_size() const = 0;

    /**
     * @brief Returns total flash capacity in bytes.
     */
    virtual size_t get_total_size() const = 0;
};

} // namespace hal
