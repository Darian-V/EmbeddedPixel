/**
 * @file test_time_source.cpp
 * @brief Tier 1 & Tier 2 Unit and Boundary Tests for hal::ITimeSource & DWT Accumulator.
 * 
 * Verifies:
 * - hal::ITimeSource pure virtual contract compliance
 * - Monotonic 64-bit microsecond counter across single and multi-turn 32-bit rollovers
 * - Frequency scaling for 600 MHz (Nucleo-H7S3L8: 600 cycles/us) and 480 MHz (PixelJam: 480 cycles/us)
 * - Exact rollover boundaries at UINT32_MAX (4,294,967,295 cycles)
 * - Zero-elapsed-cycle invocations
 * - Multi-hour / multi-day simulated long-duration accumulation without integer overflow or precision loss
 * - Strict monotonicity invariant: get_time_us(t2) >= get_time_us(t1) for all t2 >= t1
 */

#include <cstdint>
#include <cstddef>
#include <cstdio>
#include <cstring>
#include <cassert>
#include <vector>

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
        printf("  [FAIL] %s:%d: %s (Expected: %llu, Actual: %llu)\n", \
               __FILE__, __LINE__, msg, (unsigned long long)(expected), (unsigned long long)(actual)); \
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

// ── Interface Contract Definition ─────────────────────────────────────────────
namespace hal {
class ITimeSource {
public:
    virtual ~ITimeSource() = default;
    virtual void init() = 0;
    virtual uint64_t get_time_us() = 0;
    virtual uint32_t get_frequency_hz() const = 0;
};

/**
 * @brief Software Accumulator model implementing Cortex-M7 DWT 32-to-64 bit conversion.
 * 
 * Hardware CYCCNT is 32-bit unsigned. When current_cyc < last_cyc, a rollover has occurred.
 * Elapsed cycles = (uint32_t)(current_cyc - last_cyc).
 * Total 64-bit cycles are accumulated monotonically and scaled by (freq_hz / 1,000,000).
 */
class DwtAccumulatorCore {
private:
    uint32_t m_freq_hz;
    uint32_t m_cycles_per_us;
    uint32_t m_last_cyccnt;
    uint64_t m_accumulated_cycles;
    bool     m_initialized;

public:
    explicit DwtAccumulatorCore(uint32_t freq_hz = 600000000)
        : m_freq_hz(freq_hz),
          m_cycles_per_us(freq_hz / 1000000),
          m_last_cyccnt(0),
          m_accumulated_cycles(0),
          m_initialized(false) {}

    void init(uint32_t initial_cyccnt = 0) {
        m_last_cyccnt = initial_cyccnt;
        m_accumulated_cycles = 0;
        m_initialized = true;
    }

    uint64_t update_with_raw_cyccnt(uint32_t raw_cyccnt) {
        if (!m_initialized) {
            init(raw_cyccnt);
            return 0;
        }
        uint32_t delta_cycles = raw_cyccnt - m_last_cyccnt; // Handles unsigned 32-bit wrap naturally
        m_accumulated_cycles += delta_cycles;
        m_last_cyccnt = raw_cyccnt;

        if (m_cycles_per_us == 0) return 0;
        return m_accumulated_cycles / m_cycles_per_us;
    }

    uint64_t get_total_cycles() const { return m_accumulated_cycles; }
    uint32_t get_frequency_hz() const { return m_freq_hz; }
};

/**
 * @brief Simulated DWT Hardware Time Source for testing.
 */
class MockDwtTimeSource : public ITimeSource {
private:
    DwtAccumulatorCore m_core;
    uint32_t m_simulated_cyccnt;

public:
    explicit MockDwtTimeSource(uint32_t freq_hz = 600000000)
        : m_core(freq_hz), m_simulated_cyccnt(0) {}

    void init() override {
        m_simulated_cyccnt = 0;
        m_core.init(0);
    }

    void set_cyccnt(uint32_t cyccnt) {
        m_simulated_cyccnt = cyccnt;
    }

    void advance_cycles(uint32_t cycles) {
        m_simulated_cyccnt += cycles;
    }

    void advance_us(uint64_t us) {
        uint64_t cycles = us * (m_core.get_frequency_hz() / 1000000ULL);
        m_simulated_cyccnt += static_cast<uint32_t>(cycles);
    }

    uint64_t get_time_us() override {
        return m_core.update_with_raw_cyccnt(m_simulated_cyccnt);
    }

    uint32_t get_frequency_hz() const override {
        return m_core.get_frequency_hz();
    }
};

} // namespace hal

using namespace hal;

// ── Tier 1 Feature Tests (>= 5 tests) ─────────────────────────────────────────

void test_initialization_and_frequency() {
    MockDwtTimeSource source_600m(600000000);
    source_600m.init();
    TEST_ASSERT_EQ(source_600m.get_frequency_hz(), 600000000U, "Frequency 600MHz check");
    TEST_ASSERT_EQ(source_600m.get_time_us(), 0ULL, "Initial time must be 0 us");

    MockDwtTimeSource source_480m(480000000);
    source_480m.init();
    TEST_ASSERT_EQ(source_480m.get_frequency_hz(), 480000000U, "Frequency 480MHz check");
    TEST_ASSERT_EQ(source_480m.get_time_us(), 0ULL, "Initial time must be 0 us");
}

void test_linear_time_advance_600mhz() {
    MockDwtTimeSource src(600000000); // 600 cycles/us
    src.init();

    // Advance 600 cycles = 1 us
    src.advance_cycles(600);
    TEST_ASSERT_EQ(src.get_time_us(), 1ULL, "1 us at 600MHz");

    // Advance 60,000 cycles = 100 us
    src.advance_cycles(60000);
    TEST_ASSERT_EQ(src.get_time_us(), 101ULL, "101 us at 600MHz");

    // Advance 600,000,000 cycles = 1,000,000 us (1 second)
    src.advance_cycles(600000000);
    TEST_ASSERT_EQ(src.get_time_us(), 1000101ULL, "1s + 101us at 600MHz");
}

void test_linear_time_advance_480mhz() {
    MockDwtTimeSource src(480000000); // 480 cycles/us
    src.init();

    // Advance 480 cycles = 1 us
    src.advance_cycles(480);
    TEST_ASSERT_EQ(src.get_time_us(), 1ULL, "1 us at 480MHz");

    // Advance 48,000 cycles = 100 us
    src.advance_cycles(48000);
    TEST_ASSERT_EQ(src.get_time_us(), 101ULL, "101 us at 480MHz");

    // Advance 480,000,000 cycles = 1,000,000 us (1 second)
    src.advance_cycles(480000000);
    TEST_ASSERT_EQ(src.get_time_us(), 1000101ULL, "1s + 101us at 480MHz");
}

void test_zero_elapsed_cycles_stability() {
    MockDwtTimeSource src(600000000);
    src.init();
    src.advance_cycles(1200); // 2 us
    uint64_t t1 = src.get_time_us();
    TEST_ASSERT_EQ(t1, 2ULL, "t1 is 2 us");

    // Multiple consecutive calls with zero elapsed cycles
    for (int i = 0; i < 100; ++i) {
        uint64_t t2 = src.get_time_us();
        TEST_ASSERT_EQ(t2, 2ULL, "Time must remain invariant when no cycles advance");
    }
}

void test_sub_microsecond_cycle_accumulation() {
    MockDwtTimeSource src(600000000); // 600 cycles = 1 us
    src.init();

    // Advance by 100 cycles 6 times (each step < 1 us, total = 600 cycles = 1 us)
    for (int i = 0; i < 5; ++i) {
        src.advance_cycles(100);
        TEST_ASSERT_EQ(src.get_time_us(), 0ULL, "Sub-microsecond truncation check");
    }
    src.advance_cycles(100); // 6th step -> reaches 600 cycles
    TEST_ASSERT_EQ(src.get_time_us(), 1ULL, "600 cycles must yield 1 us");
}

// ── Tier 2 Boundary & Corner Cases (>= 5 tests) ───────────────────────────────

void test_single_rollover_boundary_at_uint32_max() {
    MockDwtTimeSource src(600000000); // 600 cycles/us
    src.init();

    // Set counter right before 32-bit overflow: 0xFFFFFFF0 (4,294,967,280 cycles)
    src.set_cyccnt(0xFFFFFFF0U);
    uint64_t t_before = src.get_time_us();
    TEST_ASSERT_EQ(t_before, 0xFFFFFFF0ULL / 600ULL, "Time before overflow");

    // Advance 32 cycles -> wraps past 0 to 0x00000010 (16)
    src.set_cyccnt(0x00000010U);
    uint64_t t_after = src.get_time_us();

    uint64_t expected_total_cycles = 0xFFFFFFF0ULL + 32ULL;
    uint64_t expected_us = expected_total_cycles / 600ULL;

    TEST_ASSERT_EQ(t_after, expected_us, "Time after single 32-bit overflow");
    TEST_ASSERT(t_after >= t_before, "Monotonicity preserved across rollover");
}

void test_exact_boundary_wrap_uint32_max_to_zero() {
    MockDwtTimeSource src(600000000);
    src.init();

    // Step 1: Set at exactly UINT32_MAX
    src.set_cyccnt(0xFFFFFFFFU);
    uint64_t t1 = src.get_time_us();
    TEST_ASSERT_EQ(t1, 0xFFFFFFFFULL / 600ULL, "At UINT32_MAX");

    // Step 2: Step 1 cycle to 0x00000000
    src.set_cyccnt(0x00000000U);
    uint64_t t2 = src.get_time_us();
    uint64_t expected_cycles = 0x100000000ULL;
    TEST_ASSERT_EQ(t2, expected_cycles / 600ULL, "Exact rollover to 0");
    TEST_ASSERT_EQ(t2 - t1, 0ULL, "Single cycle at 600MHz is < 1us");

    // Step 3: Advance 600 cycles from 0
    src.advance_cycles(600);
    uint64_t t3 = src.get_time_us();
    TEST_ASSERT_EQ(t3, (expected_cycles + 600ULL) / 600ULL, "600 cycles after wrap");
    TEST_ASSERT_EQ(t3 - t1, 1ULL, "1 us elapsed from UINT32_MAX");
}

void test_multi_turn_rollover_stress() {
    MockDwtTimeSource src(600000000);
    src.init();

    // Simulate 100 consecutive 32-bit rollovers (~715 seconds of simulated real-time)
    // 0xFFFFFFFF cycles at 600MHz is ~7.158 seconds per wrap.
    uint64_t previous_time_us = 0;
    for (int wrap = 0; wrap < 100; ++wrap) {
        // Advance in 4 chunks per wrap to ensure sampling within wrap window
        for (int chunk = 0; chunk < 4; ++chunk) {
            uint32_t step = 0x40000000U; // ~1.07 billion cycles (~1.78 sec)
            src.advance_cycles(step);
            uint64_t current_time_us = src.get_time_us();
            TEST_ASSERT(current_time_us > previous_time_us, "Strict monotonic progress during multi-wrap");
            previous_time_us = current_time_us;
        }
    }

    // Expected total cycles = 100 * 4 * 0x40000000 = 100 * 2^32 = 429,496,729,600 cycles
    uint64_t expected_total_cycles = 100ULL * 0x100000000ULL;
    uint64_t expected_us = expected_total_cycles / 600ULL;
    TEST_ASSERT_EQ(src.get_time_us(), expected_us, "100-turn rollover exact microsecond match");
}

void test_long_duration_accumulation_30_days() {
    MockDwtTimeSource src(600000000);
    src.init();

    // 30 days = 30 * 24 * 3600 = 2,592,000 seconds = 2,592,000,000,000 microseconds
    // Total cycles = 2.592e12 * 600 = 1,555,200,000,000,000 cycles (~362,099 rollovers)
    const uint64_t SECONDS_PER_DAY = 86400ULL;
    const uint32_t CYCLES_PER_SEC = 600000000U;

    uint64_t simulated_sec = 0;
    for (int day = 1; day <= 30; ++day) {
        for (int hour = 0; hour < 24; ++hour) {
            // Advance by 1 hour = 3600 seconds
            for (int sec = 0; sec < 3600; ++sec) {
                src.advance_cycles(CYCLES_PER_SEC);
                simulated_sec++;
            }
            uint64_t expected_us = simulated_sec * 1000000ULL;
            TEST_ASSERT_EQ(src.get_time_us(), expected_us, "Hourly check during 30-day accumulation");
        }
    }
}

void test_monotonicity_under_fine_grained_sampling() {
    MockDwtTimeSource src(600000000);
    src.init();

    // Start 1000 cycles before 32-bit rollover
    src.set_cyccnt(0xFFFFF000U);
    src.get_time_us();

    uint64_t prev_us = src.get_time_us();

    // Sample every 13 cycles for 10,000 iterations (traversing the rollover seamlessly)
    for (int i = 0; i < 10000; ++i) {
        src.advance_cycles(13);
        uint64_t cur_us = src.get_time_us();
        TEST_ASSERT(cur_us >= prev_us, "Monotonicity violation during fine-grained step");
        prev_us = cur_us;
    }
}

int main() {
    printf("===============================================================\n");
    printf(" hal::ITimeSource & DWT Accumulator Unit Tests\n");
    printf("===============================================================\n\n");

    // Tier 1
    RUN_TEST(test_initialization_and_frequency);
    RUN_TEST(test_linear_time_advance_600mhz);
    RUN_TEST(test_linear_time_advance_480mhz);
    RUN_TEST(test_zero_elapsed_cycles_stability);
    RUN_TEST(test_sub_microsecond_cycle_accumulation);

    // Tier 2
    RUN_TEST(test_single_rollover_boundary_at_uint32_max);
    RUN_TEST(test_exact_boundary_wrap_uint32_max_to_zero);
    RUN_TEST(test_multi_turn_rollover_stress);
    RUN_TEST(test_long_duration_accumulation_30_days);
    RUN_TEST(test_monotonicity_under_fine_grained_sampling);

    printf("\n===============================================================\n");
    printf(" Test Results: %d/%d Passed (%d Failed)\n", g_tests_passed, g_tests_run, g_tests_failed);
    printf("===============================================================\n");

    return (g_tests_failed == 0) ? 0 : 1;
}
