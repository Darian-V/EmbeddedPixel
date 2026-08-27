#pragma once

#include <cstdint>
#include <cstddef>

namespace hal {

/**
 * @brief High-level motion operating modes.
 */
enum class MotorMode : uint8_t {
    Disabled       = 0,
    JogVelocity    = 1,
    PositionPtp    = 2,
    InterpolatedPvt = 3,
    Fault          = 4,
};

/**
 * @brief Generic motor status snapshot.
 */
struct MotorStatus {
    bool        is_enabled{false};
    bool        is_moving{false};
    bool        in_position{false};
    bool        is_stalled{false};
    bool        is_fault{false};
    MotorMode   mode{MotorMode::Disabled};
    int32_t     current_position{0};    ///< Current physical/encoder position (pulses)
    int32_t     current_velocity{0};    ///< Current actual or commanded velocity (pulses/sec)
    int32_t     target_position{0};     ///< Desired target position (pulses)
    float       temperature_c{0.0f};    ///< Motor/driver temperature in degrees Celsius
    uint32_t    error_code{0};          ///< Driver or controller specific error code
};

/**
 * @brief Pure virtual interface for generic motor controllers and actuators.
 */
class IMotor {
public:
    virtual ~IMotor() = default;

    /**
     * @brief Enable the motor power stage / energize windings.
     * @return true on success, false on error
     */
    virtual bool enable() = 0;

    /**
     * @brief Disable the motor power stage / freewheel shaft.
     * @return true on success, false on error
     */
    virtual bool disable() = 0;

    /**
     * @brief Check if power stage is currently energized.
     */
    virtual bool is_enabled() const = 0;

    /**
     * @brief Command continuous rotation at specified velocity.
     * @param velocity_pps Velocity in pulses/sec (positive = CW/forward, negative = CCW/reverse)
     * @return true if command accepted
     */
    virtual bool jog(int32_t velocity_pps) = 0;

    /**
     * @brief Command point-to-point absolute position move.
     * @param target_position Absolute target coordinate (pulses)
     * @param speed_pps Maximum travel speed (pulses/sec)
     * @return true if command accepted
     */
    virtual bool move_absolute(int32_t target_position, uint32_t speed_pps) = 0;

    /**
     * @brief Command point-to-point relative displacement move.
     * @param delta_position Incremental distance to move (pulses)
     * @param speed_pps Maximum travel speed (pulses/sec)
     * @return true if command accepted
     */
    virtual bool move_relative(int32_t delta_position, uint32_t speed_pps) = 0;

    /**
     * @brief Stop motor movement.
     * @param emergency If true, perform immediate emergency stop; otherwise decelerate smoothly
     * @return true if stop commanded
     */
    virtual bool stop(bool emergency = false) = 0;

    /**
     * @brief Set current physical position as origin / zero coordinate.
     * @param position New coordinate value (default 0)
     * @return true on success
     */
    virtual bool set_origin(int32_t position = 0) = 0;

    /**
     * @brief Retrieve complete current motor status snapshot.
     * @param[out] status Reference to store status data
     * @return true on success
     */
    virtual bool get_status(MotorStatus& status) = 0;

    /**
     * @brief Quick query of current motor position.
     * @param[out] position Coordinate in pulses
     * @return true on success
     */
    virtual bool get_position(int32_t& position) = 0;

    /**
     * @brief Quick query of current motor speed.
     * @param[out] velocity Velocity in pulses/sec
     * @return true on success
     */
    virtual bool get_velocity(int32_t& velocity) = 0;

    /**
     * @brief Clear active faults or stall alarms.
     * @return true on success
     */
    virtual bool clear_fault() = 0;
};

} // namespace hal
