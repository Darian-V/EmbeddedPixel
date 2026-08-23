/**
 * @file test_time_manager.cpp
 * @brief Tier 1 & Tier 2 Unit, Boundary, and Mathematical Verification Tests for sys::TimeManager.
 * 
 * Verifies:
 * - sys::TimeManager lifecycle states: UNSYNCHRONIZED, CALIBRATING, LOCKED, HOLDOVER
 * - 2-phase synchronization: 2-way RTT calibration + 1 Hz periodic broadcast beacon
 * - Disciplined local clock engine formula:
 *     T_synced(t) = T_epoch_base + (t_local - t_local_ref) * (1.0 + drift_rate)
 * - Step Mode behavior for initial sync and large phase offsets (|offset| > 100,000 us)
 * - Slew Mode Proportional-Integral (PI) disciplining with rate clamped to [-500 ppm, +500 ppm]
 * - Strict monotonicity invariant (dT_synced / dt_local >= 0.9995 > 0) preventing backward time travel
 * - Zero RTT and asymmetric network latency edge cases
 * - Holdover decay and timeout triggering (5000 ms threshold)
 */

#include <cstdint>
#include <cstddef>
#include <cstdio>
#include <cmath>
#include <cstring>
#include <algorithm>

// ── Test Harness Macros ───────────────────────────────────────────────────────
static int g_tests_run = 0;
static int g_tests_passed = 0;
static int g_tests_failed = 0;

#define TEST_ASSERT(cond, msg) do { \
    if (!(cond)) { \
        printf("  [FAIL] %s:%d: %s (Assertion failed: %s)\n", __FILE__, __LINE__, msg, #cond); \
        g_tests_failed++; \
        return; \
    } \
} while (0)

#define TEST_ASSERT_EQ(actual, expected, msg) do { \
    if ((actual) != (expected)) { \
        printf("  [FAIL] %s:%d: %s (Expected: %lld, Actual: %lld)\n", \
               __FILE__, __LINE__, msg, (long long)(expected), (long long)(actual)); \
        g_tests_failed++; \
        return; \
    } \
} while (0)

#define TEST_ASSERT_NEAR(actual, expected, tolerance, msg) do { \
    int64_t diff = (int64_t)(actual) - (int64_t)(expected); \
    if (diff < 0) diff = -diff; \
    if (diff > (int64_t)(tolerance)) { \
        printf("  [FAIL] %s:%d: %s (Expected: %lld, Actual: %lld, Diff: %lld > Tol: %lld)\n", \
               __FILE__, __LINE__, msg, (long long)(expected), (long long)(actual), (long long)diff, (long long)(tolerance)); \
        g_tests_failed++; \
        return; \
    } \
} while (0)

#define RUN_TEST(fn) do { \
    g_tests_run++; \
    printf("[RUN ] %s\n", #fn); \
    int prev_failed = g_tests_failed; \
    fn(); \
    if (g_tests_failed == prev_failed) { \
        g_tests_passed++; \
        printf("  [PASS] %s\n", #fn); \
    } \
} while (0)

// ── Interface Contract & Reference Implementation ────────────────────────────
namespace hal {
class ITimeSource {
public:
    virtual ~ITimeSource() = default;
    virtual void init() = 0;
    virtual uint64_t get_time_us() = 0;
    virtual uint32_t get_frequency_hz() const = 0;
};

class ControllableTimeSource : public ITimeSource {
private:
    uint64_t m_time_us;
    uint32_t m_freq_hz;
public:
    explicit ControllableTimeSource(uint32_t freq_hz = 600000000)
        : m_time_us(0), m_freq_hz(freq_hz) {}
    void init() override { m_time_us = 0; }
    void set_time_us(uint64_t us) { m_time_us = us; }
    void advance_us(uint64_t us) { m_time_us += us; }
    uint64_t get_time_us() override { return m_time_us; }
    uint32_t get_frequency_hz() const override { return m_freq_hz; }
};
} // namespace hal

namespace sys {

enum class SyncState : uint8_t {
    UNSYNCHRONIZED = 0,
    CALIBRATING    = 1,
    LOCKED         = 2,
    HOLDOVER       = 3
};

struct TimeStats {
    SyncState state;
    int64_t   offset_us;
    uint32_t  rtt_us;
    int32_t   drift_ppm;
    uint64_t  last_sync_local_us;
    uint64_t  last_sync_utc_us;
    uint32_t  sync_count;
    uint32_t  beacon_count;
    uint32_t  step_count;
};

class TimeManager {
public:
    static constexpr int64_t  STEP_THRESHOLD_US   = 100000;   // 100 ms
    static constexpr double   MAX_SLEW_PPM        = 500.0;    // +/- 500 ppm
    static constexpr double   MAX_SLEW_RATE       = 0.000500; // 500 * 1e-6
    static constexpr uint32_t HOLDOVER_TIMEOUT_MS = 5000;     // 5 seconds

private:
    hal::ITimeSource* m_time_source;
    SyncState         m_state;

    // Disciplined Epoch Model
    uint64_t          m_epoch_base_us;    // T_epoch_base
    uint64_t          m_local_ref_us;     // t_local_ref
    double            m_drift_rate;       // rate adjustment (-0.000500 to +0.000500)

    // PI Controller State
    double            m_kp;
    double            m_ki;
    double            m_integral_error;

    // Network / Sync Metrics
    int64_t           m_last_offset_us;
    uint32_t          m_last_rtt_us;
    uint64_t          m_last_sync_local_us;
    uint64_t          m_last_sync_utc_us;
    uint32_t          m_holdover_elapsed_ms;

    // Counters
    uint32_t          m_sync_count;
    uint32_t          m_beacon_count;
    uint32_t          m_step_count;

public:
    explicit TimeManager(hal::ITimeSource* time_source)
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
          m_step_count(0) {}

    void init() {
        m_state = SyncState::UNSYNCHRONIZED;
        m_epoch_base_us = 0;
        m_local_ref_us = 0;
        m_drift_rate = 0.0;
        m_integral_error = 0.0;
        m_last_offset_us = 0;
        m_last_rtt_us = 0;
        m_holdover_elapsed_ms = 0;
        m_sync_count = 0;
        m_beacon_count = 0;
        m_step_count = 0;
    }

    void set_pi_gains(float kp, float ki) {
        m_kp = kp;
        m_ki = ki;
    }

    uint64_t get_time_us() {
        return m_time_source ? m_time_source->get_time_us() : 0;
    }

    uint64_t get_utc_epoch_us() {
        uint64_t t_local = get_time_us();
        if (m_state == SyncState::UNSYNCHRONIZED) {
            return t_local; // Local monotonic fallback
        }

        int64_t delta_local = static_cast<int64_t>(t_local - m_local_ref_us);
        double adjusted_delta = static_cast<double>(delta_local) * (1.0 + m_drift_rate);
        
        int64_t utc_calc = static_cast<int64_t>(m_epoch_base_us) + static_cast<int64_t>(adjusted_delta);
        return (utc_calc > 0) ? static_cast<uint64_t>(utc_calc) : 0;
    }

    void process_rtt_sample(uint64_t t1, uint64_t t2, uint64_t t3, uint64_t t4) {
        // NTP 4-timestamp calculation:
        // RTT = (t4 - t1) - (t3 - t2)
        // Offset = ((t2 - t1) + (t3 - t4)) / 2
        int64_t rtt = static_cast<int64_t>(t4 - t1) - static_cast<int64_t>(t3 - t2);
        if (rtt < 0) rtt = 0;
        m_last_rtt_us = static_cast<uint32_t>(rtt);

        int64_t offset = (static_cast<int64_t>(t2 - t1) + static_cast<int64_t>(t3 - t4)) / 2;
        m_last_offset_us = offset;

        // Host UTC timestamp corresponding to node local time t2:
        // host_utc_at_t2 = t1 + (rtt / 2)
        uint64_t host_utc_at_t2 = t1 + (m_last_rtt_us / 2);

        m_sync_count++;
        m_holdover_elapsed_ms = 0;

        // Step Mode check
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

    void process_beacon(uint64_t master_utc_us, uint64_t local_rx_us, uint32_t seq, uint8_t stratum) {
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

    void update(uint32_t delta_ms) {
        if (m_state == SyncState::LOCKED) {
            m_holdover_elapsed_ms += delta_ms;
            if (m_holdover_elapsed_ms >= HOLDOVER_TIMEOUT_MS) {
                m_state = SyncState::HOLDOVER;
            }
        } else if (m_state == SyncState::HOLDOVER) {
            m_holdover_elapsed_ms += delta_ms;
        }
    }

    void force_step(int64_t offset_correction_us) {
        uint64_t current_utc = get_utc_epoch_us();
        uint64_t t_local = get_time_us();
        apply_step(current_utc + offset_correction_us, t_local);
    }

    TimeStats get_stats() const {
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

private:
    void apply_step(uint64_t target_utc_us, uint64_t local_ref_us) {
        m_epoch_base_us = target_utc_us;
        m_local_ref_us = local_ref_us;
        m_drift_rate = 0.0;
        m_integral_error = 0.0;
        m_step_count++;
    }

    void apply_slew(int64_t offset_us) {
        // error = remote - local = -offset_us
        double error_us = static_cast<double>(-offset_us);
        
        m_integral_error += error_us * 0.001; // dt ~ 1 sec / scale
        
        double correction_rate = (m_kp * (error_us / 1000000.0)) + (m_ki * m_integral_error);
        
        // Clamp to [-500 ppm, +500 ppm]
        if (correction_rate > MAX_SLEW_RATE) {
            correction_rate = MAX_SLEW_RATE;
        } else if (correction_rate < -MAX_SLEW_RATE) {
            correction_rate = -MAX_SLEW_RATE;
        }
        
        // Update epoch reference to current point to prevent discontinuity
        uint64_t current_utc = get_utc_epoch_us();
        uint64_t current_local = get_time_us();
        m_epoch_base_us = current_utc;
        m_local_ref_us = current_local;
        m_drift_rate = correction_rate;
    }
};

} // namespace sys

using namespace hal;
using namespace sys;

// ── Tier 1 Feature Tests (>= 5 tests) ─────────────────────────────────────────

void test_timemanager_initial_state() {
    ControllableTimeSource ts;
    TimeManager tm(&ts);
    tm.init();

    TimeStats stats = tm.get_stats();
    TEST_ASSERT(stats.state == SyncState::UNSYNCHRONIZED, "Initial state must be UNSYNCHRONIZED");
    TEST_ASSERT_EQ(stats.sync_count, 0U, "Initial sync_count");
    TEST_ASSERT_EQ(stats.beacon_count, 0U, "Initial beacon_count");
    TEST_ASSERT_EQ(stats.step_count, 0U, "Initial step_count");
    
    // In unsynchronized state, get_utc_epoch_us() returns raw local time
    ts.set_time_us(12345);
    TEST_ASSERT_EQ(tm.get_utc_epoch_us(), 12345ULL, "Unsynchronized UTC returns local");
}

void test_initial_step_sync_via_rtt() {
    ControllableTimeSource ts;
    TimeManager tm(&ts);
    tm.init();

    // Node is at local time 10,000 us. Host UTC is ~1,700,000,000,000,000 us (~54 years offset)
    ts.set_time_us(10000);

    uint64_t t1 = 1700000000000000ULL; // Host TX
    uint64_t t2 = 10000ULL;             // Node RX
    uint64_t t3 = 10050ULL;             // Node TX
    uint64_t t4 = 1700000000000600ULL; // Host RX
    // RTT = (t4 - t1) - (t3 - t2) = 600 - 50 = 550 us
    // Offset = ((10000 - 1700000000000000) + (10050 - 1700000000000600)) / 2

    tm.process_rtt_sample(t1, t2, t3, t4);

    TimeStats stats = tm.get_stats();
    TEST_ASSERT(stats.state == SyncState::CALIBRATING, "State should be CALIBRATING after initial RTT");
    TEST_ASSERT_EQ(stats.rtt_us, 550U, "Calculated RTT 550 us");
    TEST_ASSERT_EQ(stats.step_count, 1U, "Step count incremented");

    // Local time advances 1000 us
    ts.advance_us(1000);
    uint64_t expected_utc = t1 + (stats.rtt_us / 2) + 1000ULL;
    TEST_ASSERT_EQ(tm.get_utc_epoch_us(), expected_utc, "Synchronized UTC tracks local advance");
}

void test_beacon_processing_and_lock() {
    ControllableTimeSource ts;
    TimeManager tm(&ts);
    tm.init();

    // 1. Initial 2-way RTT calibration
    ts.set_time_us(1000000); // Node at 1 sec uptime
    tm.process_rtt_sample(1724432000000000ULL, 1000000, 1000050, 1724432000000550ULL); // RTT = 500 us

    // 2. 1 second later, master sends 1 Hz beacon
    ts.advance_us(1000000); // Node at 2 sec uptime (local = 2,000,000 us)
    uint64_t beacon_master_utc = 1724432001000000ULL;
    tm.process_beacon(beacon_master_utc, ts.get_time_us(), 1, 1);

    TimeStats stats = tm.get_stats();
    TEST_ASSERT(stats.state == SyncState::LOCKED, "State should transition to LOCKED");
    TEST_ASSERT_EQ(stats.beacon_count, 1U, "Beacon count is 1");
}

void test_slew_disciplining_small_offset() {
    ControllableTimeSource ts;
    TimeManager tm(&ts);
    tm.init();

    // Calibrate at base epoch
    ts.set_time_us(1000000);
    tm.process_rtt_sample(1724432000000000ULL, 1000000, 1000050, 1724432000000550ULL);

    // Node receives beacon where host is +50 us ahead (small offset -> should SLEW, not step)
    ts.advance_us(1000000);
    uint64_t target_beacon_utc = 1724432001000050ULL;
    tm.process_beacon(target_beacon_utc, ts.get_time_us(), 2, 1);

    TimeStats stats = tm.get_stats();
    TEST_ASSERT(stats.state == SyncState::LOCKED, "State should remain LOCKED");
    TEST_ASSERT_EQ(stats.step_count, 1U, "Step count must not increment for small offset");
    TEST_ASSERT(stats.drift_ppm > 0, "Drift rate should be positive to catch up");
    TEST_ASSERT(stats.drift_ppm <= 500, "Drift rate must not exceed +500 ppm");
}

void test_holdover_state_machine_transition() {
    ControllableTimeSource ts;
    TimeManager tm(&ts);
    tm.init();

    // Lock the clock
    ts.set_time_us(1000000);
    tm.process_rtt_sample(1724432000000000ULL, 1000000, 1000050, 1724432000000550ULL);
    ts.advance_us(1000000);
    tm.process_beacon(1724432001000000ULL, ts.get_time_us(), 1, 1);

    TEST_ASSERT(tm.get_stats().state == SyncState::LOCKED, "State is LOCKED");

    // Advance 4900 ms -> should still be LOCKED (< 5000 ms)
    tm.update(4900);
    TEST_ASSERT(tm.get_stats().state == SyncState::LOCKED, "Should remain LOCKED at 4900 ms");

    // Advance another 200 ms -> total 5100 ms -> triggers HOLDOVER
    tm.update(200);
    TEST_ASSERT(tm.get_stats().state == SyncState::HOLDOVER, "Should transition to HOLDOVER after 5100 ms");

    // Resume beacon -> recovers to LOCKED
    ts.advance_us(5100000);
    tm.process_beacon(1724432007000000ULL, ts.get_time_us(), 2, 1);
    TEST_ASSERT(tm.get_stats().state == SyncState::LOCKED, "Should recover to LOCKED on beacon resumption");
}

// ── Tier 2 Boundary & Mathematical Edge Cases (>= 5 tests) ───────────────────

void test_clamping_upper_limit_plus_500_ppm() {
    ControllableTimeSource ts;
    TimeManager tm(&ts);
    tm.init();

    ts.set_time_us(1000000);
    tm.process_rtt_sample(1724432000000000ULL, 1000000, 1000050, 1724432000000550ULL);

    // Large offset just below 100 ms step threshold: +99,000 us error
    ts.advance_us(1000000);
    uint64_t target_beacon_utc = 1724432001099000ULL;
    tm.process_beacon(target_beacon_utc, ts.get_time_us(), 2, 1);

    TimeStats stats = tm.get_stats();
    TEST_ASSERT_EQ(stats.drift_ppm, 500, "Drift rate must clamp exactly at +500 ppm");
    TEST_ASSERT_EQ(stats.step_count, 1U, "Must not step for 99ms offset");
}

void test_clamping_lower_limit_minus_500_ppm() {
    ControllableTimeSource ts;
    TimeManager tm(&ts);
    tm.init();

    ts.set_time_us(1000000);
    tm.process_rtt_sample(1724432000000000ULL, 1000000, 1000050, 1724432000000550ULL);

    // Large negative offset just within slew zone: -99,000 us error
    ts.advance_us(1000000);
    uint64_t target_beacon_utc = 1724432000901000ULL;
    tm.process_beacon(target_beacon_utc, ts.get_time_us(), 2, 1);

    TimeStats stats = tm.get_stats();
    TEST_ASSERT_EQ(stats.drift_ppm, -500, "Drift rate must clamp exactly at -500 ppm");
    TEST_ASSERT_EQ(stats.step_count, 1U, "Must not step for -99ms offset");
}

void test_step_mode_threshold_boundary_100ms() {
    ControllableTimeSource ts;
    TimeManager tm(&ts);
    tm.init();

    ts.set_time_us(1000000);
    tm.process_rtt_sample(1724432000000000ULL, 1000000, 1000050, 1724432000000550ULL);

    // Test exactly at threshold: 100,000 us -> SLEW mode
    ts.advance_us(1000000);
    tm.process_beacon(1724432001100000ULL, ts.get_time_us(), 2, 1);
    TEST_ASSERT_EQ(tm.get_stats().step_count, 1U, "100,000 us offset should use Slew mode");

    // Test beyond threshold: 100,001 us -> STEP mode
    ts.advance_us(1000000);
    tm.process_beacon(1724432002200001ULL, ts.get_time_us(), 3, 1);
    TEST_ASSERT_EQ(tm.get_stats().step_count, 2U, ">100ms offset must trigger Step mode immediately");
}

void test_strict_monotonicity_during_max_negative_slew() {
    ControllableTimeSource ts;
    TimeManager tm(&ts);
    tm.init();

    ts.set_time_us(1000000);
    tm.process_rtt_sample(1724432000000000ULL, 1000000, 1000050, 1724432000000550ULL);

    // Apply max negative slew (-500 ppm)
    ts.advance_us(1000000);
    tm.process_beacon(1724432000950000ULL, ts.get_time_us(), 2, 1);
    TEST_ASSERT_EQ(tm.get_stats().drift_ppm, -500, "Ensure max negative slew is active");

    // Sample 100,000 microsecond increments (1 us step each)
    uint64_t prev_synced = tm.get_utc_epoch_us();
    for (int i = 0; i < 100000; ++i) {
        ts.advance_us(1); // 1 microsecond local advance
        uint64_t cur_synced = tm.get_utc_epoch_us();
        
        // Strict Monotonicity Assertion: cur_synced >= prev_synced
        TEST_ASSERT(cur_synced >= prev_synced, "MONOTONICITY VIOLATION: Clock moved backwards!");
        prev_synced = cur_synced;
    }
}

void test_zero_rtt_boundary_condition() {
    ControllableTimeSource ts;
    TimeManager tm(&ts);
    tm.init();

    // Node is in direct loopback / zero delay: t4 == t1, t3 == t2
    ts.set_time_us(5000);
    uint64_t t_now = 1724432000000000ULL;
    tm.process_rtt_sample(t_now, 5000, 5000, t_now);

    TimeStats stats = tm.get_stats();
    TEST_ASSERT_EQ(stats.rtt_us, 0U, "Zero RTT must calculate to 0 us");
    TEST_ASSERT_EQ(tm.get_utc_epoch_us(), t_now, "Synced time matches host UTC exactly on zero RTT");
}

int main() {
    printf("===============================================================\n");
    printf(" sys::TimeManager Unit & Boundary Verification Tests\n");
    printf("===============================================================\n\n");

    // Tier 1
    RUN_TEST(test_timemanager_initial_state);
    RUN_TEST(test_initial_step_sync_via_rtt);
    RUN_TEST(test_beacon_processing_and_lock);
    RUN_TEST(test_slew_disciplining_small_offset);
    RUN_TEST(test_holdover_state_machine_transition);

    // Tier 2
    RUN_TEST(test_clamping_upper_limit_plus_500_ppm);
    RUN_TEST(test_clamping_lower_limit_minus_500_ppm);
    RUN_TEST(test_step_mode_threshold_boundary_100ms);
    RUN_TEST(test_strict_monotonicity_during_max_negative_slew);
    RUN_TEST(test_zero_rtt_boundary_condition);

    printf("\n===============================================================\n");
    printf(" Test Results: %d/%d Passed (%d Failed)\n", g_tests_passed, g_tests_run, g_tests_failed);
    printf("===============================================================\n");

    return (g_tests_failed == 0) ? 0 : 1;
}
