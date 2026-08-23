#include "Stm32H7DwtTimeSource.h"

#if defined(STM32H7S3xx) || defined(STM32H743xx) || defined(USE_HAL_DRIVER) || defined(__CORTEX_M) || defined(__arm__)
#include "stm32h7rsxx.h"
#include "stm32h7rsxx_hal.h"
#endif

namespace stm32 {
namespace h7 {

Stm32H7DwtTimeSource::Stm32H7DwtTimeSource()
    : m_last_cyccnt(0),
      m_accumulated_cycles(0),
      m_cycles_per_us(600),
      m_freq_hz(600000000),
      m_initialized(false)
{
}

Stm32H7DwtTimeSource::Stm32H7DwtTimeSource(uint32_t nominal_freq_hz)
    : m_last_cyccnt(0),
      m_accumulated_cycles(0),
      m_cycles_per_us(nominal_freq_hz / 1000000U),
      m_freq_hz(nominal_freq_hz),
      m_initialized(false)
{
}

void Stm32H7DwtTimeSource::init() {
#if defined(CoreDebug) && defined(DWT)
    // 1. Enable Trace Peripheral (DEMCR.TRCENA)
    CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;

    // 2. Unlock DWT Access via Lock Access Register (ARMv7E-M / ARMv8-M DWT LAR Key 0xC5ACCE55)
    #if defined(DWT_LAR) || defined(__CM7_CMSIS_VERSION) || defined(__CORTEX_M)
    DWT->LAR = 0xC5ACCE55UL;
    #endif

    // 3. Reset and enable cycle counter (CTRL.CYCCNTENA)
    DWT->CYCCNT = 0;
    DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;

    // 4. Determine frequency from SystemCoreClock if available
    #if defined(SystemCoreClock) || defined(USE_HAL_DRIVER)
    if (SystemCoreClock > 0) {
        m_freq_hz = SystemCoreClock;
        m_cycles_per_us = SystemCoreClock / 1000000U;
    }
    #endif

    m_last_cyccnt = DWT->CYCCNT;
#else
    m_last_cyccnt = 0;
#endif

    m_accumulated_cycles = 0;
    if (m_cycles_per_us == 0) {
        m_cycles_per_us = 1;
    }
    m_initialized = true;
}

uint64_t Stm32H7DwtTimeSource::get_time_us() {
    if (!m_initialized) {
        init();
    }

#if defined(CoreDebug) && defined(DWT) && defined(__arm__)
    // Thread/ISR-safe atomic delta accumulation via PRIMASK critical section
    uint32_t primask = __get_PRIMASK();
    __disable_irq();

    uint32_t now_cyc = DWT->CYCCNT;
    uint32_t delta = now_cyc - m_last_cyccnt;
    m_accumulated_cycles += delta;
    m_last_cyccnt = now_cyc;
    uint64_t total_cycles = m_accumulated_cycles;
    uint32_t cycles_per_us = m_cycles_per_us;

    __set_PRIMASK(primask);
#else
    // Fallback / Non-ARM read
    uint32_t now_cyc = m_last_cyccnt;
    #if defined(DWT)
    now_cyc = DWT->CYCCNT;
    #endif
    uint32_t delta = now_cyc - m_last_cyccnt;
    m_accumulated_cycles += delta;
    m_last_cyccnt = now_cyc;
    uint64_t total_cycles = m_accumulated_cycles;
    uint32_t cycles_per_us = m_cycles_per_us;
#endif

    if (cycles_per_us == 0) {
        return 0;
    }
    return total_cycles / cycles_per_us;
}

uint32_t Stm32H7DwtTimeSource::get_frequency_hz() const {
    return m_freq_hz;
}

} // namespace h7
} // namespace stm32
