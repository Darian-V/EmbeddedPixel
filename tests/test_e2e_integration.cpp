/**
 * @file test_e2e_integration.cpp
 * @brief Tier 3 Cross-Feature Combination and Pairwise Integration Tests.
 * 
 * Verifies:
 * - Pairwise interactions between hal::ITimeSource (32-to-64 bit DWT rollover),
 *   sys::TimeManager (PI slew disciplining engine), and net::proto (Wire protocol packets).
 * - Hardware oscillator crystal skew compensation (-333 ppm drift disciplined to host UTC).
 * - High-speed telemetry packet generation (StreamPayloadHeader) with active clock disciplining.
 * - Monotonicity and continuity when DWT counter wraps DURING active PI slewing.
 * - Telemetry packet timestamp coherence during step mode transitions.
 * - Integration with Discovery Pong diagnostics and feature flag reporting.
 */

#include <cstdint>
#include <cstddef>
#include <cstdio>
#include <cmath>
#include <cstring>
#include <vector>
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

// ── Integrated Definitions ────────────────────────────────────────────────────
namespace hal {
class ITimeSource {
public:
    virtual ~ITimeSource() = default;
    virtual void init() = 0;
    virtual uint64_t get_time_us() = 0;
    virtual uint32_t get_frequency_hz() const = 0;
};

class SimulatedDwtSource : public ITimeSource {
private:
    uint32_t m_nominal_freq_hz;
    double   m_actual_freq_hz;
    uint32_t m_last_cyccnt;
    uint64_t m_accumulated_cycles;
    uint32_t m_raw_cyccnt;
    bool     m_initialized;

public:
    SimulatedDwtSource(uint32_t nominal_freq_hz = 600000000, double actual_freq_hz = 600000000.0)
        : m_nominal_freq_hz(nominal_freq_hz),
          m_actual_freq_hz(actual_freq_hz),
          m_last_cyccnt(0),
          m_accumulated_cycles(0),
          m_raw_cyccnt(0),
          m_initialized(false) {}

    void init() override {
        m_last_cyccnt = 0;
        m_accumulated_cycles = 0;
        m_raw_cyccnt = 0;
        m_initialized = true;
    }

    void set_raw_cyccnt(uint32_t cyccnt) {
        m_raw_cyccnt = cyccnt;
    }

    void advance_physical_time_us(uint64_t physical_us) {
        double cycles_double = static_cast<double>(physical_us) * (m_actual_freq_hz / 1000000.0);
        uint32_t cycles = static_cast<uint32_t>(cycles_double);
        m_raw_cyccnt += cycles;
    }

    uint64_t get_time_us() override {
        if (!m_initialized) init();
        uint32_t delta = m_raw_cyccnt - m_last_cyccnt;
        m_accumulated_cycles += delta;
        m_last_cyccnt = m_raw_cyccnt;
        return m_accumulated_cycles / (m_nominal_freq_hz / 1000000ULL);
    }

    uint32_t get_frequency_hz() const override { return m_nominal_freq_hz; }
};
} // namespace hal

namespace net::proto {
#pragma pack(push, 1)
struct PE_Header {
    uint16_t magic;
    uint8_t  version;
    uint8_t  flags;
    uint16_t node_id;
    uint16_t msg_type;
    uint32_t seq_num;
    uint16_t payload_len;
    uint16_t crc16;
};

struct StreamPayloadHeader {
    uint64_t timestamp_us;
    uint32_t stream_tag;
    uint16_t sample_rate_hz;
    uint16_t sample_count;
    uint16_t channel_count;
    uint16_t sample_type;
};

struct PayloadDiscoveryPong {
    uint32_t challenge_id;
    uint16_t node_id;
    uint16_t node_state;
    uint32_t ip_addr;
    uint8_t  mac_addr[6];
    uint16_t board_id;
    uint32_t fw_version;
    uint32_t uptime_ms;
    uint32_t hw_uid[3];
    uint32_t bootloader_version;
    uint32_t feature_flags;
};
#pragma pack(pop)

constexpr uint32_t FEAT_TIME_SYNC = (1 << 9);

inline uint16_t crc16_ccitt(const void* data, size_t length, uint16_t seed = 0xFFFF) {
    const uint8_t* p = static_cast<const uint8_t*>(data);
    uint16_t crc = seed;
    for (size_t i = 0; i < length; ++i) {
        crc ^= (static_cast<uint16_t>(p[i]) << 8);
        for (int b = 0; b < 8; ++b) {
            if (crc & 0x8000) crc = (crc << 1) ^ 0x1021;
            else crc = crc << 1;
        }
    }
    return crc;
}
} // namespace net::proto

namespace sys {
enum class SyncState : uint8_t {
    UNSYNCHRONIZED = 0,
    CALIBRATING    = 1,
    LOCKED         = 2,
    HOLDOVER       = 3
};

class TimeManager {
public:
    static constexpr int64_t  STEP_THRESHOLD_US   = 100000;
    static constexpr double   MAX_SLEW_RATE       = 0.000500; // +/- 500 ppm

private:
    hal::ITimeSource* m_time_source;
    SyncState         m_state;
    uint64_t          m_epoch_base_us;
    uint64_t          m_local_ref_us;
    double            m_drift_rate;
    double            m_kp;
    double            m_ki;
    double            m_integral_error;
    uint32_t          m_last_rtt_us;
    int64_t           m_last_offset_us;
    uint32_t          m_step_count;

public:
    explicit TimeManager(hal::ITimeSource* ts)
        : m_time_source(ts),
          m_state(SyncState::UNSYNCHRONIZED),
          m_epoch_base_us(0),
          m_local_ref_us(0),
          m_drift_rate(0.0),
          m_kp(0.20),
          m_ki(0.02),
          m_integral_error(0.0),
          m_last_rtt_us(0),
          m_last_offset_us(0),
          m_step_count(0) {}

    void init() {
        m_state = SyncState::UNSYNCHRONIZED;
        m_epoch_base_us = 0;
        m_local_ref_us = 0;
        m_drift_rate = 0.0;
        m_integral_error = 0.0;
        m_last_rtt_us = 0;
        m_last_offset_us = 0;
        m_step_count = 0;
    }

    SyncState get_state() const { return m_state; }
    int32_t get_drift_ppm() const { return static_cast<int32_t>(std::round(m_drift_rate * 1000000.0)); }
    uint32_t get_step_count() const { return m_step_count; }

    uint64_t get_utc_epoch_us() {
        uint64_t t_local = m_time_source ? m_time_source->get_time_us() : 0;
        if (m_state == SyncState::UNSYNCHRONIZED) return t_local;

        int64_t delta_local = static_cast<int64_t>(t_local - m_local_ref_us);
        double adjusted_delta = static_cast<double>(delta_local) * (1.0 + m_drift_rate);
        int64_t utc_calc = static_cast<int64_t>(m_epoch_base_us) + static_cast<int64_t>(adjusted_delta);
        return (utc_calc > 0) ? static_cast<uint64_t>(utc_calc) : 0;
    }

    void process_rtt_sample(uint64_t t1, uint64_t t2, uint64_t t3, uint64_t t4) {
        int64_t rtt = static_cast<int64_t>(t4 - t1) - static_cast<int64_t>(t3 - t2);
        if (rtt < 0) rtt = 0;
        m_last_rtt_us = static_cast<uint32_t>(rtt);
        int64_t offset = (static_cast<int64_t>(t2 - t1) + static_cast<int64_t>(t3 - t4)) / 2;
        m_last_offset_us = offset;

        uint64_t host_utc_at_t2 = t1 + (m_last_rtt_us / 2);
        if (m_state == SyncState::UNSYNCHRONIZED || std::abs(offset) > STEP_THRESHOLD_US) {
            apply_step(host_utc_at_t2, t2);
            m_state = SyncState::CALIBRATING;
        } else {
            apply_slew(offset);
            m_state = SyncState::LOCKED;
        }
    }

    void process_beacon(uint64_t master_utc_us, uint64_t local_rx_us) {
        uint64_t expected_target_utc = master_utc_us + (m_last_rtt_us / 2);
        uint64_t current_synced = get_utc_epoch_us();
        int64_t phase_error = static_cast<int64_t>(expected_target_utc) - static_cast<int64_t>(current_synced);
        m_last_offset_us = -phase_error;

        if (m_state == SyncState::UNSYNCHRONIZED || std::abs(phase_error) > STEP_THRESHOLD_US) {
            apply_step(expected_target_utc, local_rx_us);
            m_state = SyncState::LOCKED;
        } else {
            apply_slew(-phase_error);
            m_state = SyncState::LOCKED;
        }
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
        double error_us = static_cast<double>(-offset_us);
        m_integral_error += error_us * 0.001;
        double correction = (m_kp * (error_us / 1000000.0)) + (m_ki * m_integral_error);
        if (correction > MAX_SLEW_RATE) correction = MAX_SLEW_RATE;
        else if (correction < -MAX_SLEW_RATE) correction = -MAX_SLEW_RATE;

        uint64_t current_utc = get_utc_epoch_us();
        uint64_t current_local = m_time_source ? m_time_source->get_time_us() : 0;
        m_epoch_base_us = current_utc;
        m_local_ref_us = current_local;
        m_drift_rate = correction;
    }
};
} // namespace sys

using namespace hal;
using namespace net::proto;
using namespace sys;

// ── Tier 3 Cross-Feature Combination Tests ────────────────────────────────────

void test_rollover_during_active_pi_slew() {
    SimulatedDwtSource dwt(600000000, 600000000.0);
    dwt.init();
    TimeManager tm(&dwt);
    tm.init();

    // Start 1,000,000 cycles before 32-bit rollover
    dwt.set_raw_cyccnt(0xFFF00000U);
    uint64_t local_init = dwt.get_time_us();

    // Initial 2-way RTT calibration
    uint64_t host_base_utc = 1724432000000000ULL;
    tm.process_rtt_sample(host_base_utc, local_init, local_init + 50, host_base_utc + 550);

    // Apply active slew (+200 ppm) via beacon with positive offset
    dwt.advance_physical_time_us(1000000); // Advance 1 sec -> wraps DWT across UINT32_MAX!
    tm.process_beacon(host_base_utc + 1000050ULL, dwt.get_time_us());

    TEST_ASSERT_EQ(tm.get_state(), SyncState::LOCKED, "Locked state across wrap");
    TEST_ASSERT(tm.get_drift_ppm() > 0, "Slew rate active");

    // Continuous fine-grained sampling across next 5,000 points
    uint64_t prev_utc = tm.get_utc_epoch_us();
    for (int i = 0; i < 5000; ++i) {
        dwt.advance_physical_time_us(10); // 10 us advance
        uint64_t cur_utc = tm.get_utc_epoch_us();
        TEST_ASSERT(cur_utc >= prev_utc, "Monotonicity preserved across DWT wrap during active slew");
        prev_utc = cur_utc;
    }
}

void test_telemetry_stream_header_stamping_pipeline() {
    SimulatedDwtSource dwt(600000000, 600000000.0);
    dwt.init();
    TimeManager tm(&dwt);
    tm.init();

    // Calibrate node
    uint64_t host_utc = 1724432000000000ULL;
    tm.process_rtt_sample(host_utc, 0, 50, host_utc + 550);

    // Generate 100 high-speed telemetry packets (1 kHz = 1000 us interval)
    uint64_t prev_ts = 0;
    for (int seq = 1; seq <= 100; ++seq) {
        dwt.advance_physical_time_us(1000); // 1 ms advance

        StreamPayloadHeader header;
        header.timestamp_us = tm.get_utc_epoch_us();
        header.stream_tag = 0x544E4352; // 'CNTR'
        header.sample_rate_hz = 1000;
        header.sample_count = 1;
        header.channel_count = 1;
        header.sample_type = 4; // UINT32

        TEST_ASSERT(header.timestamp_us > prev_ts, "Telemetry timestamp monotonically increases");
        TEST_ASSERT(header.timestamp_us >= host_utc, "Telemetry timestamp is in host UTC domain");
        prev_ts = header.timestamp_us;
    }
}

void test_hardware_oscillator_drift_compensation() {
    // Node crystal has a hardware skew: ticks at 599.8 MHz instead of 600.0 MHz (-333.3 ppm)
    SimulatedDwtSource slow_crystal(600000000, 599800000.0);
    slow_crystal.init();
    TimeManager tm(&slow_crystal);
    tm.init();

    uint64_t host_utc = 1724432000000000ULL;
    tm.process_rtt_sample(host_utc, 0, 50, host_utc + 550);

    // Host sends 1 Hz beacons for 20 simulated seconds
    for (int sec = 1; sec <= 20; ++sec) {
        slow_crystal.advance_physical_time_us(1000000); // Exactly 1 real physical second
        uint64_t beacon_utc = host_utc + (sec * 1000000ULL);
        tm.process_beacon(beacon_utc, slow_crystal.get_time_us());
    }

    // PI loop should have identified negative hardware drift and compensated with positive slew rate
    int32_t drift_ppm = tm.get_drift_ppm();
    TEST_ASSERT(drift_ppm > 250 && drift_ppm <= 500, "PI loop compensated for ~333 ppm crystal skew");

    // Phase error against true host UTC should be locked closely
    uint64_t current_host_utc = host_utc + 20000000ULL;
    uint64_t node_utc = tm.get_utc_epoch_us();
    int64_t error_us = static_cast<int64_t>(node_utc) - static_cast<int64_t>(current_host_utc);
    if (error_us < 0) error_us = -error_us;

    TEST_ASSERT(error_us < 200, "Compensated clock is tightly phase locked to host (< 200 us error)");
}

void test_discovery_pong_feature_flags_and_status() {
    PayloadDiscoveryPong pong;
    memset(&pong, 0, sizeof(pong));
    pong.challenge_id = 0x12345678;
    pong.node_id = 1;
    pong.node_state = 2; // STREAMING
    pong.board_id = 0x0001; // Nucleo-H7S3L8
    pong.feature_flags = FEAT_TIME_SYNC | (1 << 1); // FEAT_TIME_SYNC + FEAT_TELEMETRY_STREAM

    TEST_ASSERT((pong.feature_flags & FEAT_TIME_SYNC) != 0, "FEAT_TIME_SYNC flag present in Pong");
    TEST_ASSERT_EQ(sizeof(PayloadDiscoveryPong), 48, "Discovery pong size must remain 48 bytes");
}

int main() {
    printf("===============================================================\n");
    printf(" Tier 3 Cross-Feature Combination Integration Tests\n");
    printf("===============================================================\n\n");

    RUN_TEST(test_rollover_during_active_pi_slew);
    RUN_TEST(test_telemetry_stream_header_stamping_pipeline);
    RUN_TEST(test_hardware_oscillator_drift_compensation);
    RUN_TEST(test_discovery_pong_feature_flags_and_status);

    printf("\n===============================================================\n");
    printf(" Test Results: %d/%d Passed (%d Failed)\n", g_tests_passed, g_tests_run, g_tests_failed);
    printf("===============================================================\n");

    return (g_tests_failed == 0) ? 0 : 1;
}
