#pragma once
#include "ITimeSource.h"
#include <cstdint>

namespace stm32 {
namespace h7 {

/**
 * @brief Cortex-M7 DWT Cycle Counter implementation of hal::ITimeSource for STM32H7.
 * 
 * Utilizes DWT->CYCCNT running at full CPU core clock frequency (e.g. 600 MHz on Nucleo-H7S3L8,
 * 480 MHz on PixelJam) and maintains a monotonic 64-bit microsecond counter across 32-bit rollovers.
 */
class Stm32H7DwtTimeSource : public hal::ITimeSource {
public:
    Stm32H7DwtTimeSource();
    explicit Stm32H7DwtTimeSource(uint32_t nominal_freq_hz);

    void init() override;
    uint64_t get_time_us() override;
    uint32_t get_frequency_hz() const override;

private:
    uint32_t m_last_cyccnt;
    uint64_t m_accumulated_cycles;
    uint32_t m_cycles_per_us;
    uint32_t m_freq_hz;
    bool     m_initialized;
};

} // namespace h7
} // namespace stm32
