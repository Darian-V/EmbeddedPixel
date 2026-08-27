#pragma once

#include "Uim342Driver.h"
#include <cstdint>
#include <cstddef>

namespace app::mreg {

/**
 * @brief Interpolated Trajectory Planner built on top of Uim342Driver.
 *
 * Generates smooth mathematical trajectories and feeds them to the motor's
 * PVT cubic spline engine using high-throughput QF (Quick Feed) frames.
 */
class MregTrajectoryPlanner {
public:
    explicit MregTrajectoryPlanner(drivers::uim342::Uim342Driver& driver);

    bool execute_sine_wave(int32_t amplitude_pulses, float frequency_hz, float duration_sec);
    bool execute_s_curve(int32_t target_pulses, uint32_t max_speed_pps);

private:
    drivers::uim342::Uim342Driver& driver_;
};

} // namespace app::mreg
