#pragma once
#include <cstdint>

namespace hal {

/**
 * @brief Hardware monotonic microsecond time source abstraction.
 */
class ITimeSource {
public:
    virtual ~ITimeSource() = default;

    /**
     * @brief Initialize hardware timers / DWT cycle counter.
     */
    virtual void init() = 0;

    /**
     * @brief Get monotonic microsecond count since initialization.
     * @return 64-bit microsecond counter (strictly monotonic, non-wrapping).
     */
    virtual uint64_t get_time_us() = 0;

    /**
     * @brief Get base counter frequency in Hz.
     * @return Frequency in Hz (e.g. 600,000,000 on Nucleo-H7S3L8, 480,000,000 on PixelJam).
     */
    virtual uint32_t get_frequency_hz() const = 0;
};

} // namespace hal
