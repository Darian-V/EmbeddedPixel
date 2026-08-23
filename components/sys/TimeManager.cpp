#include "TimeManager.h"
#include <cmath>
#include <cstdlib>
#include <algorithm>

namespace sys {

TimeManager::TimeManager(hal::ITimeSource* time_source)
    : m_time_source(time_source),
      m_state(SyncState::UNSYNCHRONIZED),
      m_epoch_base_us(0),
      m_local_ref_us(0),
      m_drift_rate(0.0),
      m_kp(0.20),
      m_ki(0.02),
      m_integral_error(0.0),
      m_last_offset_us(0),
      m_last_rtt_us(0),
      m_last_sync_local_us(0),
      m_last_sync_utc_us(0),
      m_holdover_elapsed_ms(0),
      m_sync_count(0),
      m_beacon_count(0),
      m_step_count(0)
{
}

void TimeManager::init() {
    m_state = SyncState::UNSYNCHRONIZED;
    m_epoch_base_us = 0;
    m_local_ref_us = 0;
    m_drift_rate = 0.0;
    m_integral_error = 0.0;
    m_last_offset_us = 0;
    m_last_rtt_us = 0;
    m_last_sync_local_us = 0;
    m_last_sync_utc_us = 0;
    m_holdover_elapsed_ms = 0;
    m_sync_count = 0;
    m_beacon_count = 0;
    m_step_count = 0;

    if (m_time_source) {
        m_time_source->init();
    }
}

void TimeManager::set_pi_gains(float kp, float ki) {
    m_kp = static_cast<double>(kp);
    m_ki = static_cast<double>(ki);
}

uint64_t TimeManager::get_time_us() {
    return m_time_source ? m_time_source->get_time_us() : 0;
}

uint64_t TimeManager::get_utc_epoch_us() {
    uint64_t t_local = get_time_us();
    if (m_state == SyncState::UNSYNCHRONIZED) {
        return t_local; // Local monotonic fallback
    }

    int64_t delta_local = static_cast<int64_t>(t_local - m_local_ref_us);
    double adjusted_delta = static_cast<double>(delta_local) * (1.0 + m_drift_rate);
    
    int64_t utc_calc = static_cast<int64_t>(m_epoch_base_us) + static_cast<int64_t>(adjusted_delta);
    return (utc_calc > 0) ? static_cast<uint64_t>(utc_calc) : 0;
}

void TimeManager::process_rtt_sample(uint64_t t1, uint64_t t2, uint64_t t3, uint64_t t4) {
    // NTP 4-timestamp calculation:
    // RTT = (t4 - t1) - (t3 - t2)
    // Offset = ((t2 - t1) + (t3 - t4)) / 2
    int64_t rtt = static_cast<int64_t>(t4 - t1) - static_cast<int64_t>(t3 - t2);
    if (rtt < 0) {
        rtt = 0;
    }
    m_last_rtt_us = static_cast<uint32_t>(rtt);

    int64_t offset = (static_cast<int64_t>(t2 - t1) + static_cast<int64_t>(t3 - t4)) / 2;
    m_last_offset_us = offset;

    // Host UTC timestamp corresponding to node local time t2:
    // host_utc_at_t2 = t1 + (rtt / 2)
    uint64_t host_utc_at_t2 = t1 + (m_last_rtt_us / 2);

    m_sync_count++;
    m_holdover_elapsed_ms = 0;

    // Step Mode check: phase reset on initial sync or when |offset| > STEP_THRESHOLD_US
    if (m_state == SyncState::UNSYNCHRONIZED || std::abs(offset) > STEP_THRESHOLD_US) {
        apply_step(host_utc_at_t2, t2);
        m_state = SyncState::CALIBRATING;
    } else {
        // Slew Mode update
        apply_slew(offset);
        m_state = SyncState::LOCKED;
    }

    m_last_sync_local_us = t2;
    m_last_sync_utc_us = host_utc_at_t2;
}

void TimeManager::process_beacon(uint64_t master_utc_us, uint64_t local_rx_us, uint32_t seq, uint8_t stratum) {
    (void)seq;
    (void)stratum;
    m_beacon_count++;
    m_holdover_elapsed_ms = 0;

    // With one-way delay estimation (half RTT from calibration):
    uint64_t expected_target_utc = master_utc_us + (m_last_rtt_us / 2);
    
    // Compare with current disciplined clock at local_rx_us
    uint64_t current_synced = get_utc_epoch_us();
    int64_t phase_error = static_cast<int64_t>(expected_target_utc) - static_cast<int64_t>(current_synced);
    m_last_offset_us = -phase_error; // Convention: offset = local - remote

    if (m_state == SyncState::UNSYNCHRONIZED || std::abs(phase_error) > STEP_THRESHOLD_US) {
        apply_step(expected_target_utc, local_rx_us);
        m_state = SyncState::LOCKED;
    } else {
        apply_slew(-phase_error);
        m_state = SyncState::LOCKED;
    }

    m_last_sync_local_us = local_rx_us;
    m_last_sync_utc_us = expected_target_utc;
}

void TimeManager::update(uint32_t delta_ms) {
    if (m_state == SyncState::LOCKED) {
        m_holdover_elapsed_ms += delta_ms;
        if (m_holdover_elapsed_ms >= HOLDOVER_TIMEOUT_MS) {
            m_state = SyncState::HOLDOVER;
        }
    } else if (m_state == SyncState::HOLDOVER) {
        m_holdover_elapsed_ms += delta_ms;
    }
}

void TimeManager::force_step(int64_t offset_correction_us) {
    uint64_t current_utc = get_utc_epoch_us();
    uint64_t t_local = get_time_us();
    apply_step(current_utc + offset_correction_us, t_local);
}

TimeStats TimeManager::get_stats() const {
    TimeStats stats;
    stats.state = m_state;
    stats.offset_us = m_last_offset_us;
    stats.rtt_us = m_last_rtt_us;
    stats.drift_ppm = static_cast<int32_t>(std::round(m_drift_rate * 1000000.0));
    stats.last_sync_local_us = m_last_sync_local_us;
    stats.last_sync_utc_us = m_last_sync_utc_us;
    stats.sync_count = m_sync_count;
    stats.beacon_count = m_beacon_count;
    stats.step_count = m_step_count;
    return stats;
}

int32_t TimeManager::get_drift_ppm() const {
    return static_cast<int32_t>(std::round(m_drift_rate * 1000000.0));
}

void TimeManager::apply_step(uint64_t target_utc_us, uint64_t local_ref_us) {
    m_epoch_base_us = target_utc_us;
    m_local_ref_us = local_ref_us;
    m_drift_rate = 0.0;
    m_integral_error = 0.0;
    m_step_count++;
}

void TimeManager::apply_slew(int64_t offset_us) {
    // error = remote - local = -offset_us
    double error_us = static_cast<double>(-offset_us);
    
    m_integral_error += error_us * 0.001; // dt ~ 1 sec / scale
    
    double correction_rate = (m_kp * (error_us / 1000000.0)) + (m_ki * m_integral_error);
    
    // Clamp strictly within [-500 ppm, +500 ppm]
    if (correction_rate > MAX_SLEW_RATE) {
        correction_rate = MAX_SLEW_RATE;
    } else if (correction_rate < -MAX_SLEW_RATE) {
        correction_rate = -MAX_SLEW_RATE;
    }
    
    // Update epoch reference to current point to maintain phase continuity
    uint64_t current_utc = get_utc_epoch_us();
    uint64_t current_local = get_time_us();
    m_epoch_base_us = current_utc;
    m_local_ref_us = current_local;
    m_drift_rate = correction_rate;
}

} // namespace sys
