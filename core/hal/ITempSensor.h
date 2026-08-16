#pragma once
#include <cstdint>

namespace hal {

/**
 * @brief Abstract interface for on-chip or board temperature sensor.
 */
class ITempSensor {
public:
    virtual ~ITempSensor() = default;

    /**
     * @brief Initialize and start the temperature sensor.
     * @return true on success, false on error.
     */
    virtual bool init() = 0;

    /**
     * @brief Read current calibrated temperature in degrees Celsius.
     * @param[out] temp_c Temperature in degrees Celsius.
     * @return true on success, false on error.
     */
    virtual bool get_temperature(int32_t& temp_c) = 0;

    /**
     * @brief Stop the temperature sensor to reduce power.
     */
    virtual void stop() = 0;
};

} // namespace hal
