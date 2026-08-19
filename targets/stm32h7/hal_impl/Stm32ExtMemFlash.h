#pragma once

#include "IFlashDriver.h"
#include <stdint.h>
#include <stddef.h>

namespace stm32::h7 {

/**
 * @brief Flash driver implementation for STM32H7 / STM32H7RS External NOR Flash (XSPI / ExtMem).
 */
class Stm32ExtMemFlash : public hal::IFlashDriver {
public:
    Stm32ExtMemFlash(uint32_t baseAddress = 0x70000000, size_t totalSize = 64 * 1024 * 1024);
    ~Stm32ExtMemFlash() override = default;

    bool init() override;
    bool erase(uint32_t offset, size_t size) override;
    bool write(uint32_t offset, const void* data, size_t size) override;
    bool read(uint32_t offset, void* buffer, size_t size) override;

    size_t get_sector_size() const override { return 4096; } // 4 KB sector granularity
    size_t get_page_size() const override { return 256; }    // 256 Byte page size
    size_t get_total_size() const override { return total_size_; }

private:
    uint32_t base_address_;
    size_t   total_size_;
    bool     initialized_;
};

} // namespace stm32::h7
