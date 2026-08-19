#include "Stm32ExtMemFlash.h"
#include <string.h>
#include "stm32h7rsxx_hal.h"

#define RAM_FUNC __attribute__((noinline, section(".RamFunc")))

#if !defined(BOOTLOADER)

static RAM_FUNC void ram_xspi_abort() {
    XSPI2->CR |= XSPI_CR_ABORT;
    while ((XSPI2->CR & XSPI_CR_ABORT) != 0U) {}
    XSPI2->FCR = XSPI_FCR_CTCF | XSPI_FCR_CTEF | XSPI_FCR_CSMF;
    XSPI2->CR &= ~XSPI_CR_FMODE; // FMODE = 00 (Indirect Write)
    XSPI2->CR |= XSPI_CR_EN;
}

static RAM_FUNC void ram_xspi_restore_mapped() {
    XSPI2->CR &= ~XSPI_CR_FMODE;
    XSPI2->CR |= (XSPI_CR_FMODE_1 | XSPI_CR_FMODE_0); // 0b11 = Memory Mapped Mode
    XSPI2->CR |= XSPI_CR_EN;
}

static RAM_FUNC void ram_delay_loops(uint32_t loops) {
    for (volatile uint32_t i = 0; i < loops; ++i) {
        __NOP();
    }
}

static RAM_FUNC void ram_xspi_write_enable() {
    // WREN (0x06F9 in 8D-8D-8D)
    XSPI2->CCR = XSPI_CCR_ISIZE_0 | (7U << XSPI_CCR_IMODE_Pos);
    XSPI2->TCR = 0;
    XSPI2->IR = 0x06F9;
    uint32_t timeout = 100000;
    while ((XSPI2->SR & XSPI_SR_TCF) == 0 && --timeout > 0) {}
    XSPI2->FCR = XSPI_FCR_CTCF;
}

static RAM_FUNC bool ram_xspi_erase_4k(uint32_t address) {
    ram_xspi_write_enable();

    // 4KB Sector Erase (0x21DE in 8D-8D-8D, 32-bit address)
    // Order: CCR -> TCR -> IR -> AR (writing AR triggers transaction)
    XSPI2->CCR = XSPI_CCR_ISIZE_0 | (7U << XSPI_CCR_IMODE_Pos) |
                 XSPI_CCR_ADSIZE_0 | XSPI_CCR_ADSIZE_1 | (7U << XSPI_CCR_ADMODE_Pos) | XSPI_CCR_ADDTR;
    XSPI2->TCR = 0;
    XSPI2->IR = 0x21DE;
    XSPI2->AR = address;

    uint32_t timeout = 100000;
    while ((XSPI2->SR & XSPI_SR_TCF) == 0 && --timeout > 0) {}
    XSPI2->FCR = XSPI_FCR_CTCF;

    // MX25 4KB Sector Erase: ~45 ms delay at 600 MHz
    ram_delay_loops(600000);
    return true;
}

static RAM_FUNC bool ram_xspi_program_page(uint32_t address, const uint8_t* data, uint32_t len) {
    ram_xspi_write_enable();

    // Page Program (0x12ED in 8D-8D-8D, 32-bit address, up to 256 bytes)
    // Order: CCR -> TCR -> DLR -> IR -> AR -> DR stream
    XSPI2->CCR = XSPI_CCR_ISIZE_0 | (7U << XSPI_CCR_IMODE_Pos) |
                 XSPI_CCR_ADSIZE_0 | XSPI_CCR_ADSIZE_1 | (7U << XSPI_CCR_ADMODE_Pos) | XSPI_CCR_ADDTR |
                 (7U << XSPI_CCR_DMODE_Pos) | XSPI_CCR_DDTR;
    XSPI2->TCR = 0;
    XSPI2->DLR = len - 1U;
    XSPI2->IR = 0x12ED;
    XSPI2->AR = address;

    for (uint32_t i = 0; i < len; ++i) {
        uint32_t timeout = 100000;
        while ((XSPI2->SR & (XSPI_SR_FTF | XSPI_SR_TCF)) == 0 && --timeout > 0) {}
        *reinterpret_cast<volatile uint8_t*>(&XSPI2->DR) = data[i];
    }

    uint32_t timeout = 100000;
    while ((XSPI2->SR & XSPI_SR_TCF) == 0 && --timeout > 0) {}
    XSPI2->FCR = XSPI_FCR_CTCF;

    // MX25 Page Program: ~1.5 ms delay at 600 MHz
    ram_delay_loops(20000);
    return true;
}

#endif // !BOOTLOADER

namespace stm32::h7 {

Stm32ExtMemFlash::Stm32ExtMemFlash(uint32_t baseAddress, size_t totalSize)
    : base_address_(baseAddress),
      total_size_(totalSize),
      initialized_(false) {}

bool Stm32ExtMemFlash::init() {
    initialized_ = true;
    return true;
}

RAM_FUNC bool Stm32ExtMemFlash::erase(uint32_t offset, size_t size) {
    if (offset + size > total_size_) {
        return false;
    }

#if !defined(BOOTLOADER)
    uint32_t cur = offset;
    uint32_t end = offset + size;
    while (cur < end) {
        __disable_irq();
        ram_xspi_abort();

        ram_xspi_erase_4k(cur);

        ram_xspi_restore_mapped();
        SCB_InvalidateDCache();
        SCB_InvalidateICache();
        __enable_irq();

        cur += 4096;
    }

    return true;
#else
    return false;
#endif
}

RAM_FUNC bool Stm32ExtMemFlash::write(uint32_t offset, const void* data, size_t size) {
    if (offset + size > total_size_ || data == nullptr || size == 0) {
        return false;
    }

#if !defined(BOOTLOADER)
    const uint8_t* ptr = static_cast<const uint8_t*>(data);
    uint32_t cur_offset = offset;
    uint32_t remaining = size;

    while (remaining > 0) {
        uint32_t page_offset = cur_offset % 256;
        uint32_t chunk = 256 - page_offset;
        if (chunk > remaining) {
            chunk = remaining;
        }

        __disable_irq();
        ram_xspi_abort();

        ram_xspi_program_page(cur_offset, ptr, chunk);

        ram_xspi_restore_mapped();
        SCB_InvalidateDCache();
        SCB_InvalidateICache();
        __enable_irq();

        cur_offset += chunk;
        ptr += chunk;
        remaining -= chunk;
    }

    return true;
#else
    return false;
#endif
}

bool Stm32ExtMemFlash::read(uint32_t offset, void* buffer, size_t size) {
    if (offset + size > total_size_ || buffer == nullptr || size == 0) {
        return false;
    }

    SCB_InvalidateDCache();
    const uint8_t* src = reinterpret_cast<const uint8_t*>(base_address_ + offset);
    memcpy(buffer, src, size);
    return true;
}

} // namespace stm32::h7
