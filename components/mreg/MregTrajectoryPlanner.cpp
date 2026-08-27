#include "MregTrajectoryPlanner.h"
#include <cmath>
#include <cstdio>

namespace app::mreg {

constexpr float MREG_PI = 3.14159265358979323846f;

MregTrajectoryPlanner::MregTrajectoryPlanner(drivers::uim342::Uim342Driver& driver)
    : driver_(driver) {}

bool MregTrajectoryPlanner::execute_sine_wave(int32_t amplitude_pulses, float frequency_hz, float duration_sec) {
    if (frequency_hz <= 0.0f || duration_sec <= 0.0f) {
        return false;
    }

    if (!driver_.is_enabled()) {
        driver_.enable();
    }

    driver_.reset_pvt_queue();
    driver_.configure_pvt_mode(drivers::uim342::PvtMode::Single, 3);

    constexpr uint8_t dt_ms = 50;
    float dt_sec = static_cast<float>(dt_ms) / 1000.0f;
    uint32_t total_points = static_cast<uint32_t>(duration_sec / dt_sec);
    if (total_points > 250) {
        total_points = 250;
    }

    float omega = 2.0f * MREG_PI * frequency_hz;

    for (uint32_t i = 0; i < total_points; ++i) {
        float t = static_cast<float>(i + 1) * dt_sec;
        float pos_f = static_cast<float>(amplitude_pulses) * sinf(omega * t);
        float vel_f = static_cast<float>(amplitude_pulses) * omega * cosf(omega * t);

        if (i == total_points - 1) {
            vel_f = 0.0f;
        }

        int32_t p = static_cast<int32_t>(pos_f);
        int32_t v = static_cast<int32_t>(vel_f);

        if (!driver_.feed_pvt_quick(dt_ms, v, p)) {
            printf("[MREG-TRAJ] Failed to feed PVT point %lu\r\n", static_cast<unsigned long>(i));
            return false;
        }
    }

    return driver_.start_pvt(0);
}

bool MregTrajectoryPlanner::execute_s_curve(int32_t target_pulses, uint32_t max_speed_pps) {
    if (max_speed_pps == 0) {
        return false;
    }

    if (!driver_.is_enabled()) {
        driver_.enable();
    }

    driver_.reset_pvt_queue();
    driver_.configure_pvt_mode(drivers::uim342::PvtMode::Single, 3);

    constexpr uint8_t dt_ms = 100;
    constexpr uint32_t num_steps = 16;

    for (uint32_t i = 1; i <= num_steps; ++i) {
        float norm_t = static_cast<float>(i) / static_cast<float>(num_steps);
        float s = norm_t * norm_t * (3.0f - 2.0f * norm_t);
        float ds = (6.0f * norm_t - 6.0f * norm_t * norm_t) / 1.6f;

        int32_t p = static_cast<int32_t>(static_cast<float>(target_pulses) * s);
        int32_t v = static_cast<int32_t>(static_cast<float>(target_pulses) * ds);
        if (i == num_steps) {
            v = 0;
            p = target_pulses;
        }

        if (!driver_.feed_pvt_quick(dt_ms, v, p)) {
            return false;
        }
    }

    return driver_.start_pvt(0);
}

} // namespace app::mreg
