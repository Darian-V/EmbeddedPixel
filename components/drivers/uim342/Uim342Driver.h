#pragma once

#include "ICan.h"
#include "IMotor.h"
#include "Uim342Types.h"
#include <cstdint>
#include <cstddef>

namespace drivers::uim342 {

/**
 * @brief Callback signature for asynchronous UIM342 Real-Time notifications (CW = 0x5A).
 */
using NotificationCallback = void (*)(NotificationType type, uint8_t detail, void* context);

/**
 * @brief Generic UIROBOT UIM342A / UIM342SA / UIM342XSA Stepper Servo Driver.
 *
 * Implements the SimpleCAN 3.0 protocol over any hal::ICan interface and fulfills
 * the generic hal::IMotor abstraction contract.
 */
class Uim342Driver : public hal::IMotor {
public:
    /**
     * @param can_bus Reference to initialized CAN hardware abstraction
     * @param node_id Target UIM342 Motor Node ID (factory default 5)
     * @param master_id Master Controller Producer ID (standard default 4)
     */
    explicit Uim342Driver(hal::ICan& can_bus, uint8_t node_id = 5, uint8_t master_id = 4);
    ~Uim342Driver() override = default;

    // ── Static SimpleCAN 3.0 Protocol Helpers ──────────────────────────────
    static uint32_t encode_can_id(uint8_t producer_id, uint8_t consumer_id, uint8_t cw, bool req_ack = true);
    static bool decode_can_id(uint32_t can_id, uint8_t& producer_id, uint8_t& cw, bool& is_ack);

    // ── hal::IMotor Generic Interface Implementation ───────────────────────
    bool enable() override;
    bool disable() override;
    bool is_enabled() const override;
    bool jog(int32_t velocity_pps) override;
    bool move_absolute(int32_t target_position, uint32_t speed_pps) override;
    bool move_relative(int32_t delta_position, uint32_t speed_pps) override;
    bool stop(bool emergency = false) override;
    bool set_origin(int32_t position = 0) override;
    bool get_status(hal::MotorStatus& status) override;
    bool get_position(int32_t& position) override;
    virtual bool get_absolute_position(int32_t& position);
    bool get_velocity(int32_t& velocity) override;
    bool clear_fault() override;

    // ── UIM342 Specific Motion Controls ────────────────────────────────────
    bool begin_motion();
    bool configure_dynamics(uint32_t accel, uint32_t decel, uint32_t cut_in_speed, uint32_t stop_decel);
    bool set_acceleration(uint32_t accel);
    bool set_deceleration(uint32_t decel);
    bool set_cut_in_speed(uint32_t cut_in);
    bool set_stop_deceleration(uint32_t stop_decel);
    bool set_backlash_comp(uint16_t pulses);

    // ── Motor Drive & Electrical Parameters ────────────────────────────────
    bool set_drive_current(uint8_t current_tenth_a, uint8_t idle_pct = 50);
    bool set_microstepping(uint8_t microsteps);
    bool set_internally_controlled_brake(bool engage);

    // ── Software Travel Limits ─────────────────────────────────────────────
    bool set_software_limits(int32_t lower_limit, int32_t upper_limit, uint32_t max_speed);
    bool enable_software_limits(bool enable);

    // ── Interpolated PVT / PT Motion Engine ────────────────────────────────
    bool configure_pvt_mode(PvtMode mode, uint16_t queue_low_water = 3);
    bool feed_pvt_quick(uint8_t time_ms, int32_t velocity_pps, int32_t position_pulses);
    bool feed_pt(uint16_t row_idx, int32_t position_pulses);
    bool start_pvt(uint16_t start_row = 0);
    bool reset_pvt_queue();

    // ── Diagnostics & Telemetry Queries ────────────────────────────────────
    bool query_temperature(float& temp_c);
    bool query_encoder_voltage(float& volts);
    bool query_status(UimStatus& status);
    bool query_last_error(UimErrorInfo& error_info);
    bool query_device_info(UimModelInfo& model_info);

    // ── Direct Raw Instruction Dispatch ────────────────────────────────────
    bool send_raw_command(uint8_t cw, const uint8_t* payload, uint8_t len, bool req_ack = true);

    // ── Frame Dispatcher & Asynchronous Real-Time Notification ─────────────
    void process_incoming_frame(const hal::CanFrame& frame);
    void register_notification_callback(NotificationCallback cb, void* context);

    // ── ID Accessors ───────────────────────────────────────────────────────
    uint8_t get_node_id() const { return node_id_; }
    void set_node_id(uint8_t id) { node_id_ = id; }
    uint8_t get_master_id() const { return master_id_; }
    void set_master_id(uint8_t id) { master_id_ = id; }

    const UimStatus& get_cached_status() const { return cached_status_; }

private:
    hal::ICan&           can_;
    uint8_t              node_id_;
    uint8_t              master_id_;
    UimStatus            cached_status_;
    NotificationCallback notification_cb_{nullptr};
    void*                notification_context_{nullptr};

    // Internal helper to send command and wait for matching synchronous ACK
    bool send_and_wait_ack(uint8_t cw, const uint8_t* tx_data, uint8_t tx_len,
                           uint8_t* rx_data, uint8_t& rx_len, uint32_t timeout_ms = 50);
};

} // namespace drivers::uim342
