#pragma once

#include "Uim342Driver.h"
#include <cstdint>
#include <cstddef>

namespace app::mreg {

enum class SupervisorState : uint8_t {
    Uninitialized     = 0,
    IdleDisabled      = 1,
    IdleEnabled       = 2,
    Homing            = 3,
    Jogging           = 4,
    Positioning       = 5,
    RunningTrajectory = 6,
    StallDetected     = 7,
    FaultLockdown     = 8,
};

const char* state_to_string(SupervisorState state);

/**
 * @brief High-level application supervisor for UIM342 motor control.
 *
 * Implements state management, homing sequencing, stall/fault recovery policies,
 * and high-level motion lifecycle orchestration.
 */
class MregSupervisor {
public:
    explicit MregSupervisor(drivers::uim342::Uim342Driver& driver);

    bool init();
    void update();

    // High-Level User Actions
    bool power_on();
    bool power_off();
    bool command_jog(int32_t velocity_pps);
    bool command_move_abs(int32_t target_position, uint32_t speed_pps = 3200);
    bool command_move_rel(int32_t delta_position, uint32_t speed_pps = 3200);
    bool command_start_homing();
    bool command_stop();
    bool command_emergency_stop();
    bool clear_fault();

    // State & Status
    SupervisorState get_state() const { return state_; }
    const drivers::uim342::UimStatus& get_status() const { return driver_.get_cached_status(); }
    drivers::uim342::Uim342Driver& get_driver() { return driver_; }

    // Internal Callback from CAN Driver for Real-Time Alerts
    void handle_notification(drivers::uim342::NotificationType type, uint8_t detail);

private:
    drivers::uim342::Uim342Driver& driver_;
    SupervisorState                state_{SupervisorState::Uninitialized};
    uint32_t                       state_entry_tick_{0};
    int32_t                        homing_stage_{0};

    void transition_to(SupervisorState new_state);
    void process_homing_sequence();
};

} // namespace app::mreg
