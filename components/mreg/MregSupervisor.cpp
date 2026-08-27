#include "MregSupervisor.h"
#include <cstdio>

namespace app::mreg {

const char* state_to_string(SupervisorState state) {
    switch (state) {
        case SupervisorState::Uninitialized:     return "UNINITIALIZED";
        case SupervisorState::IdleDisabled:      return "IDLE_DISABLED";
        case SupervisorState::IdleEnabled:       return "IDLE_ENABLED";
        case SupervisorState::Homing:            return "HOMING";
        case SupervisorState::Jogging:           return "JOGGING";
        case SupervisorState::Positioning:       return "POSITIONING";
        case SupervisorState::RunningTrajectory: return "RUNNING_TRAJECTORY";
        case SupervisorState::StallDetected:     return "STALL_DETECTED";
        case SupervisorState::FaultLockdown:     return "FAULT_LOCKDOWN";
        default:                                 return "UNKNOWN";
    }
}

static void on_driver_notification(drivers::uim342::NotificationType type, uint8_t detail, void* context) {
    if (context != nullptr) {
        static_cast<MregSupervisor*>(context)->handle_notification(type, detail);
    }
}

MregSupervisor::MregSupervisor(drivers::uim342::Uim342Driver& driver)
    : driver_(driver) {}

bool MregSupervisor::init() {
    driver_.register_notification_callback(on_driver_notification, this);

    // Default dynamics: Accel = 10000, Decel = 10000, CutIn = 100, StopDecel = 50000
    driver_.configure_dynamics(10000, 10000, 100, 50000);

    transition_to(SupervisorState::IdleDisabled);
    return true;
}

void MregSupervisor::transition_to(SupervisorState new_state) {
    state_ = new_state;
    state_entry_tick_++;
}

void MregSupervisor::update() {
    if (state_ == SupervisorState::Homing) {
        process_homing_sequence();
    }
}

void MregSupervisor::process_homing_sequence() {
    if (homing_stage_ == 0) {
        driver_.jog(-1600); // Reverse jog towards origin switch
        homing_stage_ = 1;
    }
}

bool MregSupervisor::power_on() {
    if (state_ == SupervisorState::FaultLockdown || state_ == SupervisorState::StallDetected) {
        return false;
    }
    if (driver_.enable()) {
        transition_to(SupervisorState::IdleEnabled);
        return true;
    }
    return false;
}

bool MregSupervisor::power_off() {
    if (driver_.disable()) {
        transition_to(SupervisorState::IdleDisabled);
        return true;
    }
    return false;
}

bool MregSupervisor::command_jog(int32_t velocity_pps) {
    if (state_ != SupervisorState::IdleEnabled && state_ != SupervisorState::Jogging) {
        if (!power_on()) {
            return false;
        }
    }
    if (driver_.jog(velocity_pps)) {
        transition_to(SupervisorState::Jogging);
        return true;
    }
    return false;
}

bool MregSupervisor::command_move_abs(int32_t target_position, uint32_t speed_pps) {
    if (state_ != SupervisorState::IdleEnabled && state_ != SupervisorState::Positioning) {
        if (!power_on()) {
            return false;
        }
    }
    if (driver_.move_absolute(target_position, speed_pps)) {
        transition_to(SupervisorState::Positioning);
        return true;
    }
    return false;
}

bool MregSupervisor::command_move_rel(int32_t delta_position, uint32_t speed_pps) {
    if (state_ != SupervisorState::IdleEnabled && state_ != SupervisorState::Positioning) {
        if (!power_on()) {
            return false;
        }
    }
    if (driver_.move_relative(delta_position, speed_pps)) {
        transition_to(SupervisorState::Positioning);
        return true;
    }
    return false;
}

bool MregSupervisor::command_start_homing() {
    if (state_ != SupervisorState::IdleEnabled) {
        if (!power_on()) {
            return false;
        }
    }
    homing_stage_ = 0;
    transition_to(SupervisorState::Homing);
    return true;
}

bool MregSupervisor::command_stop() {
    bool ok = driver_.stop(false);
    if (state_ != SupervisorState::IdleDisabled && state_ != SupervisorState::FaultLockdown) {
        transition_to(SupervisorState::IdleEnabled);
    }
    return ok;
}

bool MregSupervisor::command_emergency_stop() {
    driver_.stop(true);
    transition_to(SupervisorState::FaultLockdown);
    return true;
}

bool MregSupervisor::clear_fault() {
    driver_.clear_fault();
    transition_to(driver_.is_enabled() ? SupervisorState::IdleEnabled : SupervisorState::IdleDisabled);
    return true;
}

void MregSupervisor::handle_notification(drivers::uim342::NotificationType type, uint8_t detail) {
    if (type == drivers::uim342::NotificationType::Alarm) {
        if (detail == static_cast<uint8_t>(drivers::uim342::AlarmCode::MotorStallDetected)) {
            printf("[MREG] ALARM: Motor stall detected!\r\n");
            transition_to(SupervisorState::StallDetected);
        } else if (detail == static_cast<uint8_t>(drivers::uim342::AlarmCode::EmergencyStopLock)) {
            printf("[MREG] ALARM: Emergency stop lockdown active!\r\n");
            transition_to(SupervisorState::FaultLockdown);
        }
    } else if (type == drivers::uim342::NotificationType::PtpPositionCompleted) {
        if (state_ == SupervisorState::Positioning) {
            transition_to(SupervisorState::IdleEnabled);
        }
    }
}

} // namespace app::mreg
