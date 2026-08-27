#include "Uim342Driver.h"
#include <cstring>

namespace drivers::uim342 {

Uim342Driver::Uim342Driver(hal::ICan& can_bus, uint8_t node_id, uint8_t master_id)
    : can_(can_bus),
      node_id_(node_id),
      master_id_(master_id) {
    cached_status_ = {};
}

uint32_t Uim342Driver::encode_can_id(uint8_t producer_id, uint8_t consumer_id, uint8_t cw, bool req_ack) {
    // SID[10:0]: bits [10:6] = Producer ID[4:0], bits [5:1] = Consumer ID[4:0], bit 0 = 0
    uint32_t sid = (((static_cast<uint32_t>(producer_id) & 0x1F) << 6) |
                    ((static_cast<uint32_t>(consumer_id) & 0x1F) << 1)) & 0x07FF;

    // EID[17:0]: bits [17:16] = Producer ID[6:5], bits [15:14] = Consumer ID[6:5], bits [13:8] = 0, bits [7:0] = CW
    uint32_t eid = (((static_cast<uint32_t>(producer_id) & 0x60) >> 5) << 16) |
                   (((static_cast<uint32_t>(consumer_id) & 0x60) >> 5) << 14) |
                   (req_ack ? (static_cast<uint32_t>(cw) | FLAG_ACK_REQUEST) : (static_cast<uint32_t>(cw) & 0x7F));

    return (sid << 18) | (eid & 0x3FFFF);
}

bool Uim342Driver::decode_can_id(uint32_t can_id, uint8_t& producer_id, uint8_t& cw, bool& is_ack) {
    uint32_t sid = (can_id >> 18) & 0x07FF;
    uint32_t eid = can_id & 0x3FFFF;

    producer_id = static_cast<uint8_t>(((eid >> 11) & 0x60) | ((sid >> 6) & 0x1F));
    cw = static_cast<uint8_t>(eid & 0xFF);
    is_ack = ((cw & FLAG_ACK_REQUEST) == 0);
    return true;
}

bool Uim342Driver::send_and_wait_ack(uint8_t cw, const uint8_t* tx_data, uint8_t tx_len,
                                     uint8_t* rx_data, uint8_t& rx_len, uint32_t timeout_ms) {
    hal::CanFrame tx_frame{};
    tx_frame.id = encode_can_id(master_id_, node_id_, cw, true);
    tx_frame.is_extended = true;
    tx_frame.is_rtr = false;
    tx_frame.dlc = tx_len;
    if (tx_data != nullptr && tx_len > 0) {
        std::memcpy(tx_frame.data, tx_data, (tx_len > 8) ? 8 : tx_len);
    }

    if (!can_.transmit(tx_frame, timeout_ms)) {
        return false;
    }

    // Poll for matching ACK response
    uint32_t elapsed = 0;
    constexpr uint32_t poll_step_ms = 1;
    while (elapsed < timeout_ms) {
        hal::CanFrame rx_frame{};
        if (can_.receive(rx_frame, poll_step_ms)) {
            uint8_t rx_prod = 0;
            uint8_t rx_cw = 0;
            bool is_ack = false;
            decode_can_id(rx_frame.id, rx_prod, rx_cw, is_ack);

            // Handle asynchronous notification if it comes during polling
            if (rx_cw == CW_RT) {
                process_incoming_frame(rx_frame);
                continue;
            }

            // Check if response matches target node and instruction code
            if (rx_prod == node_id_ && (rx_cw & 0x7F) == (cw & 0x7F)) {
                rx_len = rx_frame.dlc;
                if (rx_data != nullptr && rx_len > 0) {
                    std::memcpy(rx_data, rx_frame.data, (rx_len > 8) ? 8 : rx_len);
                }
                return true;
            }
        }
        elapsed += poll_step_ms;
    }

    return false;
}

bool Uim342Driver::send_raw_command(uint8_t cw, const uint8_t* payload, uint8_t len, bool req_ack) {
    if (req_ack) {
        uint8_t rx_buf[8]{0};
        uint8_t rx_len = 0;
        return send_and_wait_ack(cw, payload, len, rx_buf, rx_len, 50);
    }

    hal::CanFrame frame{};
    frame.id = encode_can_id(master_id_, node_id_, cw, false);
    frame.is_extended = true;
    frame.is_rtr = false;
    frame.dlc = len;
    if (payload != nullptr && len > 0) {
        std::memcpy(frame.data, payload, (len > 8) ? 8 : len);
    }
    return can_.transmit(frame, 10);
}

// ── hal::IMotor Interface Implementation ───────────────────────────────────

bool Uim342Driver::enable() {
    uint8_t data[1] = {0x01};
    uint8_t rx[8]{0};
    uint8_t rlen = 0;
    bool ok = send_and_wait_ack(CW_MO, data, 1, rx, rlen);
    if (ok) {
        cached_status_.driver_on = true;
    }
    return ok;
}

bool Uim342Driver::disable() {
    uint8_t data[1] = {0x00};
    uint8_t rx[8]{0};
    uint8_t rlen = 0;
    bool ok = send_and_wait_ack(CW_MO, data, 1, rx, rlen);
    if (ok) {
        cached_status_.driver_on = false;
    }
    return ok;
}

bool Uim342Driver::is_enabled() const {
    return cached_status_.driver_on;
}

bool Uim342Driver::jog(int32_t velocity_pps) {
    uint8_t payload[4];
    payload[0] = static_cast<uint8_t>(velocity_pps & 0xFF);
    payload[1] = static_cast<uint8_t>((velocity_pps >> 8) & 0xFF);
    payload[2] = static_cast<uint8_t>((velocity_pps >> 16) & 0xFF);
    payload[3] = static_cast<uint8_t>((velocity_pps >> 24) & 0xFF);

    uint8_t rx[8]{0};
    uint8_t rlen = 0;
    if (!send_and_wait_ack(CW_JV, payload, 4, rx, rlen)) {
        return false;
    }
    return begin_motion();
}

bool Uim342Driver::move_absolute(int32_t target_position, uint32_t speed_pps) {
    // 1. Set PTP Target Absolute Position (PA)
    uint8_t pa_payload[4];
    pa_payload[0] = static_cast<uint8_t>(target_position & 0xFF);
    pa_payload[1] = static_cast<uint8_t>((target_position >> 8) & 0xFF);
    pa_payload[2] = static_cast<uint8_t>((target_position >> 16) & 0xFF);
    pa_payload[3] = static_cast<uint8_t>((target_position >> 24) & 0xFF);

    uint8_t rx[8]{0};
    uint8_t rlen = 0;
    if (!send_and_wait_ack(CW_PA, pa_payload, 4, rx, rlen)) {
        return false;
    }

    // 2. Set PTP Speed (SP)
    uint8_t sp_payload[4];
    sp_payload[0] = static_cast<uint8_t>(speed_pps & 0xFF);
    sp_payload[1] = static_cast<uint8_t>((speed_pps >> 8) & 0xFF);
    sp_payload[2] = static_cast<uint8_t>((speed_pps >> 16) & 0xFF);
    sp_payload[3] = static_cast<uint8_t>((speed_pps >> 24) & 0xFF);

    if (!send_and_wait_ack(CW_SP, sp_payload, 4, rx, rlen)) {
        return false;
    }

    // 3. Begin Motion (BG)
    return begin_motion();
}

bool Uim342Driver::move_relative(int32_t delta_position, uint32_t speed_pps) {
    // 1. Set PTP Target Relative Position (PR)
    uint8_t pr_payload[4];
    pr_payload[0] = static_cast<uint8_t>(delta_position & 0xFF);
    pr_payload[1] = static_cast<uint8_t>((delta_position >> 8) & 0xFF);
    pr_payload[2] = static_cast<uint8_t>((delta_position >> 16) & 0xFF);
    pr_payload[3] = static_cast<uint8_t>((delta_position >> 24) & 0xFF);

    uint8_t rx[8]{0};
    uint8_t rlen = 0;
    if (!send_and_wait_ack(CW_PR, pr_payload, 4, rx, rlen)) {
        return false;
    }

    // 2. Set PTP Speed (SP)
    uint8_t sp_payload[4];
    sp_payload[0] = static_cast<uint8_t>(speed_pps & 0xFF);
    sp_payload[1] = static_cast<uint8_t>((speed_pps >> 8) & 0xFF);
    sp_payload[2] = static_cast<uint8_t>((speed_pps >> 16) & 0xFF);
    sp_payload[3] = static_cast<uint8_t>((speed_pps >> 24) & 0xFF);

    if (!send_and_wait_ack(CW_SP, sp_payload, 4, rx, rlen)) {
        return false;
    }

    // 3. Begin Motion (BG)
    return begin_motion();
}

bool Uim342Driver::stop(bool emergency) {
    (void)emergency;
    uint8_t rx[8]{0};
    uint8_t rlen = 0;
    return send_and_wait_ack(CW_ST, nullptr, 0, rx, rlen);
}

bool Uim342Driver::set_origin(int32_t position) {
    (void)position;
    uint8_t rx[8]{0};
    uint8_t rlen = 0;
    return send_and_wait_ack(CW_OG, nullptr, 0, rx, rlen);
}

bool Uim342Driver::get_status(hal::MotorStatus& status) {
    UimStatus uim_st{};
    if (!query_status(uim_st)) {
        return false;
    }

    status.is_enabled = uim_st.driver_on;
    status.is_moving = !uim_st.is_stopped;
    status.in_position = uim_st.in_position;
    status.is_stalled = uim_st.is_stalled;
    status.is_fault = uim_st.has_error || uim_st.is_locked;
    status.mode = (uim_st.mode == 0) ? hal::MotorMode::JogVelocity : hal::MotorMode::PositionPtp;
    status.current_position = uim_st.absolute_position;
    status.current_velocity = uim_st.current_speed;
    status.temperature_c = uim_st.temperature_c;
    status.error_code = uim_st.has_error ? 1 : 0;
    return true;
}

bool Uim342Driver::get_position(int32_t& position) {
    UimStatus st{};
    if (!query_status(st)) {
        return false;
    }
    position = st.relative_position;
    return true;
}

bool Uim342Driver::get_absolute_position(int32_t& position) {
    UimStatus st{};
    if (!query_status(st)) {
        return false;
    }
    position = st.absolute_position;
    return true;
}

bool Uim342Driver::get_velocity(int32_t& velocity) {
    UimStatus st{};
    if (!query_status(st)) {
        return false;
    }
    velocity = st.current_speed;
    return true;
}

bool Uim342Driver::clear_fault() {
    uint8_t data[2] = {0x00, 0x00}; // MS[0] = 0 clears PAIF, TLIF, and ERR
    uint8_t rx[8]{0};
    uint8_t rlen = 0;
    return send_and_wait_ack(CW_MS, data, 2, rx, rlen);
}

// ── UIM342 Specific Motion Controls ────────────────────────────────────────

bool Uim342Driver::begin_motion() {
    uint8_t rx[8]{0};
    uint8_t rlen = 0;
    return send_and_wait_ack(CW_BG, nullptr, 0, rx, rlen);
}

bool Uim342Driver::configure_dynamics(uint32_t accel, uint32_t decel, uint32_t cut_in_speed, uint32_t stop_decel) {
    bool ok = set_acceleration(accel);
    ok = ok && set_deceleration(decel);
    ok = ok && set_cut_in_speed(cut_in_speed);
    ok = ok && set_stop_deceleration(stop_decel);
    return ok;
}

bool Uim342Driver::set_acceleration(uint32_t accel) {
    uint8_t payload[4];
    payload[0] = static_cast<uint8_t>(accel & 0xFF);
    payload[1] = static_cast<uint8_t>((accel >> 8) & 0xFF);
    payload[2] = static_cast<uint8_t>((accel >> 16) & 0xFF);
    payload[3] = static_cast<uint8_t>((accel >> 24) & 0xFF);
    uint8_t rx[8]{0};
    uint8_t rlen = 0;
    return send_and_wait_ack(CW_AC, payload, 4, rx, rlen);
}

bool Uim342Driver::set_deceleration(uint32_t decel) {
    uint8_t payload[4];
    payload[0] = static_cast<uint8_t>(decel & 0xFF);
    payload[1] = static_cast<uint8_t>((decel >> 8) & 0xFF);
    payload[2] = static_cast<uint8_t>((decel >> 16) & 0xFF);
    payload[3] = static_cast<uint8_t>((decel >> 24) & 0xFF);
    uint8_t rx[8]{0};
    uint8_t rlen = 0;
    return send_and_wait_ack(CW_DC, payload, 4, rx, rlen);
}

bool Uim342Driver::set_cut_in_speed(uint32_t cut_in) {
    uint8_t payload[4];
    payload[0] = static_cast<uint8_t>(cut_in & 0xFF);
    payload[1] = static_cast<uint8_t>((cut_in >> 8) & 0xFF);
    payload[2] = static_cast<uint8_t>((cut_in >> 16) & 0xFF);
    payload[3] = static_cast<uint8_t>((cut_in >> 24) & 0xFF);
    uint8_t rx[8]{0};
    uint8_t rlen = 0;
    return send_and_wait_ack(CW_SS, payload, 4, rx, rlen);
}

bool Uim342Driver::set_stop_deceleration(uint32_t stop_decel) {
    uint8_t payload[4];
    payload[0] = static_cast<uint8_t>(stop_decel & 0xFF);
    payload[1] = static_cast<uint8_t>((stop_decel >> 8) & 0xFF);
    payload[2] = static_cast<uint8_t>((stop_decel >> 16) & 0xFF);
    payload[3] = static_cast<uint8_t>((stop_decel >> 24) & 0xFF);
    uint8_t rx[8]{0};
    uint8_t rlen = 0;
    return send_and_wait_ack(CW_SD, payload, 4, rx, rlen);
}

bool Uim342Driver::set_backlash_comp(uint16_t pulses) {
    uint8_t payload[4] = {
        static_cast<uint8_t>(pulses & 0xFF),
        static_cast<uint8_t>((pulses >> 8) & 0xFF),
        0x00, 0x00
    };
    uint8_t rx[8]{0};
    uint8_t rlen = 0;
    return send_and_wait_ack(CW_BL, payload, 4, rx, rlen);
}

// ── Motor Drive & Electrical Parameters ────────────────────────────────────

bool Uim342Driver::set_drive_current(uint8_t current_tenth_a, uint8_t idle_pct) {
    // MT[1] = working current (5..80 = 0.5..8.0A)
    uint8_t p1[3] = {0x01, current_tenth_a, 0x00};
    uint8_t rx[8]{0};
    uint8_t rlen = 0;
    bool ok = send_and_wait_ack(CW_MT, p1, 3, rx, rlen);

    // MT[2] = idle current percentage (0..100)
    uint8_t p2[3] = {0x02, idle_pct, 0x00};
    ok = ok && send_and_wait_ack(CW_MT, p2, 3, rx, rlen);
    return ok;
}

bool Uim342Driver::set_microstepping(uint8_t microsteps) {
    // MT[0] = microsteps (1, 2, 4, 8, 16, 32, 64, 128)
    uint8_t payload[3] = {0x00, microsteps, 0x00};
    uint8_t rx[8]{0};
    uint8_t rlen = 0;
    return send_and_wait_ack(CW_MT, payload, 3, rx, rlen);
}

bool Uim342Driver::set_internally_controlled_brake(bool engage) {
    // MT[5] = 0 release, 1 engage
    uint8_t payload[3] = {0x05, static_cast<uint8_t>(engage ? 1 : 0), 0x00};
    uint8_t rx[8]{0};
    uint8_t rlen = 0;
    return send_and_wait_ack(CW_MT, payload, 3, rx, rlen);
}

// ── Software Travel Limits ─────────────────────────────────────────────────

bool Uim342Driver::set_software_limits(int32_t lower_limit, int32_t upper_limit, uint32_t max_speed) {
    uint8_t rx[8]{0};
    uint8_t rlen = 0;

    // LM[0] = max speed
    uint8_t p0[5] = {
        0x00,
        static_cast<uint8_t>(max_speed & 0xFF),
        static_cast<uint8_t>((max_speed >> 8) & 0xFF),
        static_cast<uint8_t>((max_speed >> 16) & 0xFF),
        static_cast<uint8_t>((max_speed >> 24) & 0xFF)
    };
    bool ok = send_and_wait_ack(CW_LM, p0, 5, rx, rlen);

    // LM[1] = lower working limit
    uint8_t p1[5] = {
        0x01,
        static_cast<uint8_t>(lower_limit & 0xFF),
        static_cast<uint8_t>((lower_limit >> 8) & 0xFF),
        static_cast<uint8_t>((lower_limit >> 16) & 0xFF),
        static_cast<uint8_t>((lower_limit >> 24) & 0xFF)
    };
    ok = ok && send_and_wait_ack(CW_LM, p1, 5, rx, rlen);

    // LM[2] = upper working limit
    uint8_t p2[5] = {
        0x02,
        static_cast<uint8_t>(upper_limit & 0xFF),
        static_cast<uint8_t>((upper_limit >> 8) & 0xFF),
        static_cast<uint8_t>((upper_limit >> 16) & 0xFF),
        static_cast<uint8_t>((upper_limit >> 24) & 0xFF)
    };
    ok = ok && send_and_wait_ack(CW_LM, p2, 5, rx, rlen);
    return ok;
}

bool Uim342Driver::enable_software_limits(bool enable) {
    uint8_t payload[5] = {0xFF, static_cast<uint8_t>(enable ? 1 : 0), 0x00, 0x00, 0x00};
    uint8_t rx[8]{0};
    uint8_t rlen = 0;
    return send_and_wait_ack(CW_LM, payload, 5, rx, rlen);
}

// ── Interpolated PVT / PT Motion Engine ────────────────────────────────────

bool Uim342Driver::configure_pvt_mode(PvtMode mode, uint16_t queue_low_water) {
    uint8_t rx[8]{0};
    uint8_t rlen = 0;

    // MP[4] = 0 for PVT motion
    uint8_t p4[3] = {0x04, 0x00, 0x00};
    bool ok = send_and_wait_ack(CW_MP, p4, 3, rx, rlen);

    // MP[3] = mode (0=FIFO, 1=Single, 3=Loop)
    uint8_t p3[3] = {0x03, static_cast<uint8_t>(static_cast<uint16_t>(mode) & 0xFF), 0x00};
    ok = ok && send_and_wait_ack(CW_MP, p3, 3, rx, rlen);

    // MP[5] = queue low alert threshold
    uint8_t p5[3] = {0x05, static_cast<uint8_t>(queue_low_water & 0xFF), 0x00};
    ok = ok && send_and_wait_ack(CW_MP, p5, 3, rx, rlen);
    return ok;
}

bool Uim342Driver::feed_pvt_quick(uint8_t time_ms, int32_t velocity_pps, int32_t position_pulses) {
    // QF Data Structure (CW = 0x29 / 0xA9):
    // d0 = Time (ms)
    // d1..d3 = Velocity (24-bit signed)
    // d4..d7 = Position (32-bit signed)
    uint8_t payload[8];
    payload[0] = time_ms;
    payload[1] = static_cast<uint8_t>(velocity_pps & 0xFF);
    payload[2] = static_cast<uint8_t>((velocity_pps >> 8) & 0xFF);
    payload[3] = static_cast<uint8_t>((velocity_pps >> 16) & 0xFF);

    payload[4] = static_cast<uint8_t>(position_pulses & 0xFF);
    payload[5] = static_cast<uint8_t>((position_pulses >> 8) & 0xFF);
    payload[6] = static_cast<uint8_t>((position_pulses >> 16) & 0xFF);
    payload[7] = static_cast<uint8_t>((position_pulses >> 24) & 0xFF);

    uint8_t rx[8]{0};
    uint8_t rlen = 0;
    return send_and_wait_ack(CW_QF, payload, 8, rx, rlen);
}

bool Uim342Driver::feed_pt(uint16_t row_idx, int32_t position_pulses) {
    // PT Data Structure (CW = 0x24 / 0xA4):
    // d0..d1 = Line No (row index)
    // d2..d5 = Position (32-bit signed)
    // d6..d7 = 0x00
    uint8_t payload[8];
    payload[0] = static_cast<uint8_t>(row_idx & 0xFF);
    payload[1] = static_cast<uint8_t>((row_idx >> 8) & 0xFF);
    payload[2] = static_cast<uint8_t>(position_pulses & 0xFF);
    payload[3] = static_cast<uint8_t>((position_pulses >> 8) & 0xFF);
    payload[4] = static_cast<uint8_t>((position_pulses >> 16) & 0xFF);
    payload[5] = static_cast<uint8_t>((position_pulses >> 24) & 0xFF);
    payload[6] = 0x00;
    payload[7] = 0x00;

    uint8_t rx[8]{0};
    uint8_t rlen = 0;
    return send_and_wait_ack(CW_PT, payload, 8, rx, rlen);
}

bool Uim342Driver::start_pvt(uint16_t start_row) {
    // PV = start_row
    uint8_t p[2] = {
        static_cast<uint8_t>(start_row & 0xFF),
        static_cast<uint8_t>((start_row >> 8) & 0xFF)
    };
    uint8_t rx[8]{0};
    uint8_t rlen = 0;
    if (!send_and_wait_ack(CW_PV, p, 2, rx, rlen)) {
        return false;
    }
    return begin_motion();
}

bool Uim342Driver::reset_pvt_queue() {
    uint8_t payload[3] = {0x00, 0x01, 0x00};
    uint8_t rx[8]{0};
    uint8_t rlen = 0;
    return send_and_wait_ack(CW_MP, payload, 3, rx, rlen);
}

// ── Diagnostics & Telemetry Queries ────────────────────────────────────────

bool Uim342Driver::query_temperature(float& temp_c) {
    uint8_t rx[8]{0};
    uint8_t rlen = 0;
    if (!send_and_wait_ack(CW_TI, nullptr, 0, rx, rlen)) {
        return false;
    }
    if (rlen >= 2) {
        int16_t raw = static_cast<int16_t>(rx[0] | (static_cast<uint16_t>(rx[1]) << 8));
        temp_c = static_cast<float>(raw) / 10.0f;
        cached_status_.temperature_c = temp_c;
        return true;
    }
    return false;
}

bool Uim342Driver::query_encoder_voltage(float& volts) {
    uint8_t payload[1] = {0x03}; // QE[3] = Absolute Encoder Battery Voltage
    uint8_t rx[8]{0};
    uint8_t rlen = 0;
    if (!send_and_wait_ack(CW_QE, payload, 1, rx, rlen)) {
        return false;
    }
    if (rlen >= 3) {
        uint16_t mv = static_cast<uint16_t>(rx[1] | (static_cast<uint16_t>(rx[2]) << 8));
        volts = static_cast<float>(mv) / 1000.0f;
        cached_status_.encoder_battery_v = volts;
        return true;
    }
    return false;
}

bool Uim342Driver::query_status(UimStatus& status) {
    uint8_t rx[8]{0};
    uint8_t rlen = 0;

    // 1. Query MS[0] (Flags and Relative Position)
    uint8_t p0[1] = {0x00};
    if (!send_and_wait_ack(CW_MS, p0, 1, rx, rlen)) {
        return false;
    }
    if (rlen >= 8) {
        uint8_t d1 = rx[1];
        uint8_t d2 = rx[2];

        cached_status_.mode = d1 & 0x03;
        cached_status_.driver_on = ((d1 & (1 << 2)) != 0);
        cached_status_.digital_inputs = (d1 >> 3) & 0x07;
        cached_status_.digital_output = (d1 >> 6) & 0x01;

        cached_status_.is_stopped = ((d2 & (1 << 0)) != 0);
        cached_status_.in_position = ((d2 & (1 << 1)) != 0);
        cached_status_.is_stalled = ((d2 & (1 << 3)) != 0);
        cached_status_.is_locked = ((d2 & (1 << 5)) != 0);
        cached_status_.has_error = ((d2 & (1 << 7)) != 0);

        uint32_t pr_raw = static_cast<uint32_t>(rx[4]) |
                          (static_cast<uint32_t>(rx[5]) << 8) |
                          (static_cast<uint32_t>(rx[6]) << 16) |
                          (static_cast<uint32_t>(rx[7]) << 24);
        cached_status_.relative_position = static_cast<int32_t>(pr_raw);
    }

    // 2. Query MS[1] (Speed and Absolute Position)
    uint8_t p1[1] = {0x01};
    if (!send_and_wait_ack(CW_MS, p1, 1, rx, rlen)) {
        return false;
    }
    if (rlen >= 8) {
        // 24-bit speed with sign extension
        uint32_t sp_raw = static_cast<uint32_t>(rx[1]) |
                          (static_cast<uint32_t>(rx[2]) << 8) |
                          (static_cast<uint32_t>(rx[3]) << 16);
        if (rx[3] & 0x80) {
            sp_raw |= 0xFF000000;
        }
        cached_status_.current_speed = static_cast<int32_t>(sp_raw);

        uint32_t pa_raw = static_cast<uint32_t>(rx[4]) |
                          (static_cast<uint32_t>(rx[5]) << 8) |
                          (static_cast<uint32_t>(rx[6]) << 16) |
                          (static_cast<uint32_t>(rx[7]) << 24);
        cached_status_.absolute_position = static_cast<int32_t>(pa_raw);
    }

    status = cached_status_;
    return true;
}

bool Uim342Driver::query_last_error(UimErrorInfo& error_info) {
    uint8_t p[1] = {0x00}; // ER[0]
    uint8_t rx[8]{0};
    uint8_t rlen = 0;
    if (!send_and_wait_ack(CW_ER, p, 1, rx, rlen)) {
        return false;
    }
    if (rlen >= 4) {
        error_info.error_code = rx[1];
        error_info.related_cw = rx[2];
        error_info.sub_index = rx[3];
        return true;
    }
    return false;
}

bool Uim342Driver::query_device_info(UimModelInfo& model_info) {
    uint8_t rx[8]{0};
    uint8_t rlen = 0;

    // ML (Model & Firmware)
    if (send_and_wait_ack(CW_ML, nullptr, 0, rx, rlen) && rlen >= 8) {
        model_info.model_code[0] = rx[0];
        model_info.model_code[1] = rx[1];
        model_info.model_code[2] = rx[2];
        model_info.model_code[3] = rx[3];
        model_info.fw_version = static_cast<uint16_t>(rx[4] | (static_cast<uint16_t>(rx[5]) << 8));
    }

    // SN (Serial Number)
    if (send_and_wait_ack(CW_SN, nullptr, 0, rx, rlen) && rlen >= 4) {
        model_info.serial_number = static_cast<uint32_t>(rx[0]) |
                                   (static_cast<uint32_t>(rx[1]) << 8) |
                                   (static_cast<uint32_t>(rx[2]) << 16) |
                                   (static_cast<uint32_t>(rx[3]) << 24);
    }

    return true;
}

// ── Asynchronous Real-Time Notification & Incoming Frame Routing ───────────

void Uim342Driver::process_incoming_frame(const hal::CanFrame& frame) {
    uint8_t prod = 0;
    uint8_t cw = 0;
    bool is_ack = false;
    decode_can_id(frame.id, prod, cw, is_ack);

    if (prod != node_id_) {
        return;
    }

    // Real-Time Inform (CW = 0x5A)
    if (cw == CW_RT && frame.dlc >= 1) {
        uint8_t d0 = frame.data[0];
        uint8_t detail = (frame.dlc >= 2) ? frame.data[1] : 0;

        if (d0 == 0x00) { // Alarm
            if (detail == static_cast<uint8_t>(AlarmCode::MotorStallDetected)) {
                cached_status_.is_stalled = true;
            } else if (detail == static_cast<uint8_t>(AlarmCode::EmergencyStopLock)) {
                cached_status_.is_locked = true;
            }
            cached_status_.has_error = true;
        } else if (d0 == static_cast<uint8_t>(NotificationType::PtpPositionCompleted)) {
            cached_status_.in_position = true;
            cached_status_.is_stopped = true;
            if (frame.dlc >= 8) {
                uint32_t pa_raw = static_cast<uint32_t>(frame.data[4]) |
                                  (static_cast<uint32_t>(frame.data[5]) << 8) |
                                  (static_cast<uint32_t>(frame.data[6]) << 16) |
                                  (static_cast<uint32_t>(frame.data[7]) << 24);
                cached_status_.absolute_position = static_cast<int32_t>(pa_raw);
            }
        }

        if (notification_cb_ != nullptr) {
            notification_cb_(static_cast<NotificationType>(d0), detail, notification_context_);
        }
    }
}

void Uim342Driver::register_notification_callback(NotificationCallback cb, void* context) {
    notification_cb_ = cb;
    notification_context_ = context;
}

} // namespace drivers::uim342
