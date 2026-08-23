#pragma once

#include "ITimeSource.h"
#include <cstdint>

namespace sys {

/**
 * @brief Time synchronization lifecycle states.
 */
enum class SyncState : uint8_t {
    UNSYNCHRONIZED = 0, ///< Boot state / unsynchronized local clock
    CALIBRATING    = 1, ///< Initial 2-way RTT calibration in progress
    LOCKED         = 2, ///< Phase/frequency locked to master UTC
    HOLDOVER       = 3  ///< Master beacon timeout; coasting on last calibrated drift rate
};

/**
 * @brief Time synchronization runtime statistics and diagnostics.
 */
struct TimeStats {
    SyncState state;              ///< Current sync lifecycle state
    int64_t   offset_us;          ///< Last calculated phase offset against host (us)
    uint32_t  rtt_us;             ///< Last measured network round-trip time (us)
    int32_t   drift_ppm;          ///< Current rate adjustment in parts-per-million (-500 to +500)
    uint64_t  last_sync_local_us; ///< Local timestamp of last sync/beacon (us)
    uint64_t  last_sync_utc_us;   ///< Host UTC timestamp of last sync/beacon (us)
    uint32_t  sync_count;         ///< Total number of 2-way RTT sync exchanges
    uint32_t  beacon_count;       ///< Total number of 1-way beacons received
    uint32_t  step_count;         ///< Total number of phase steps applied
};

/**
 * @brief Disciplined Clock Engine for multi-node microsecond time synchronization.
 * 
 * Maps local hardware time to global host UTC epoch using a hybrid 2-phase disciplining model:
 *   T_synced(t) = T_epoch_base + (t_local - t_local_ref) * (1.0 + drift_rate)
 * 
 * Features:
 * - Step Mode: Immediate phase correction for initial synchronization or large offsets (>100 ms).
 * - Slew Mode: Proportional-Integral (PI) rate disciplining clamped strictly to [-500 ppm, +500 ppm].
 * - Strict Monotonicity: Guaranteed positive derivative (dT/dt >= 0.9995 > 0) preventing negative time jumps.
 * - Holdover Management: Seamless holdover coasting if master beacons are interrupted.
 */
class TimeManager {
public:
    static constexpr int64_t  STEP_THRESHOLD_US   = 100000;   ///< 100 ms threshold for step vs slew
    static constexpr double   MAX_SLEW_PPM        = 500.0;    ///< +/- 500 ppm max slew
    static constexpr double   MAX_SLEW_RATE       = 0.000500; ///< 500 * 1e-6 max slew rate
    static constexpr uint32_t HOLDOVER_TIMEOUT_MS = 5000;     ///< 5 seconds holdover timeout

    explicit TimeManager(hal::ITimeSource* time_source);

    /**
     * @brief Initialize or reset time manager state.
     */
    void init();

    /**
     * @brief Get local monotonic microsecond uptime from hardware time source.
     * @return Local microsecond timestamp.
     */
    uint64_t get_time_us();

    /**
     * @brief Get synchronized UTC microsecond timestamp.
     * 
     * In UNSYNCHRONIZED state, returns local uptime.
     * In CALIBRATING, LOCKED, and HOLDOVER states, evaluates disciplined clock model.
     * Guarantees strict monotonicity.
     * @return 64-bit UTC microsecond timestamp.
     */
    uint64_t get_utc_epoch_us();

    /**
     * @brief Process a 2-way 4-timestamp RTT exchange sample.
     * @param t1 Host transmit timestamp (UTC us)
     * @param t2 Node receive timestamp (Local us)
     * @param t3 Node transmit timestamp (Local us)
     * @param t4 Host receive timestamp (UTC us)
     */
    void process_rtt_sample(uint64_t t1, uint64_t t2, uint64_t t3, uint64_t t4);

    /**
     * @brief Process a periodic 1 Hz master broadcast beacon.
     * @param master_utc_us Master broadcast UTC epoch (us)
     * @param local_rx_us Local hardware receive timestamp (us)
     * @param seq Monotonic beacon sequence number
     * @param stratum Master clock stratum level
     */
    void process_beacon(uint64_t master_utc_us, uint64_t local_rx_us, uint32_t seq = 0, uint8_t stratum = 1);

    /**
     * @brief Periodic update routine for holdover timer and status decay.
     * @param delta_ms Elapsed milliseconds since last update.
     */
    void update(uint32_t delta_ms);

    /**
     * @brief Retrieve snapshot of runtime synchronization statistics.
     * @return TimeStats structure.
     */
    TimeStats get_stats() const;

    /**
     * @brief Manually force an immediate phase step correction.
     * @param offset_correction_us Phase offset correction to add (us).
     */
    void force_step(int64_t offset_correction_us);

    /**
     * @brief Configure Proportional-Integral controller gains for slew disciplining.
     * @param kp Proportional gain (default 0.20)
     * @param ki Integral gain (default 0.02)
     */
    void set_pi_gains(float kp, float ki);

    // Ergonomic inline accessors
    SyncState get_state() const { return m_state; }
    int32_t get_drift_ppm() const;
    int64_t get_offset_us() const { return m_last_offset_us; }
    uint32_t get_rtt_us() const { return m_last_rtt_us; }
    uint32_t get_step_count() const { return m_step_count; }
    uint32_t get_sync_count() const { return m_sync_count; }
    uint32_t get_beacon_count() const { return m_beacon_count; }
    hal::ITimeSource* get_time_source() const { return m_time_source; }

private:
    void apply_step(uint64_t target_utc_us, uint64_t local_ref_us);
    void apply_slew(int64_t offset_us);

    hal::ITimeSource* m_time_source;
    SyncState         m_state;

    // Disciplined Epoch Model
    uint64_t          m_epoch_base_us;    ///< T_epoch_base
    uint64_t          m_local_ref_us;     ///< t_local_ref
    double            m_drift_rate;       ///< rate adjustment (-0.000500 to +0.000500)

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
};

} // namespace sys
