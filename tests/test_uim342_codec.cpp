#include <cassert>
#include <cstdio>
#include <cstring>
#include <vector>
#include <deque>

#include "core/hal/ICan.h"
#include "core/hal/IMotor.h"
#include "components/drivers/uim342/Uim342Driver.h"
#include "components/net/MotorTelemetryChannel.h"

using namespace drivers::uim342;

// ── Mock CAN Implementation for Host Tests ─────────────────────────────────
class MockCan : public hal::ICan {
public:
    std::vector<hal::CanFrame> transmitted_frames;
    std::deque<hal::CanFrame>  rx_queue;

    bool init(hal::CanBaudRate baud) override {
        (void)baud;
        return true;
    }

    bool transmit(const hal::CanFrame& frame, uint32_t timeout_ms) override {
        (void)timeout_ms;
        transmitted_frames.push_back(frame);
        return true;
    }

    bool receive(hal::CanFrame& frame, uint32_t timeout_ms) override {
        (void)timeout_ms;
        if (rx_queue.empty()) {
            return false;
        }
        frame = rx_queue.front();
        rx_queue.pop_front();
        return true;
    }

    bool configure_filter(const hal::CanFilter& filter) override {
        (void)filter;
        return true;
    }

    bool is_bus_off() const override { return false; }
    void recover_bus() override {}

    void clear() {
        transmitted_frames.clear();
        rx_queue.clear();
    }
};

// ── Test 1: SimpleCAN 3.0 ID Encoding & Manual Specification Matching ──────
void test_can_id_encoding() {
    printf("[TEST] Testing SimpleCAN 3.0 CAN-ID Encoding against UIROBOT Manual...\n");

    // UIROBOT Manual Page 23 Example:
    // Producer ID = 4, Consumer ID = 5, CW = 0x95 (MO with ACK) -> CAN-ID: 0x04280095
    uint32_t id_mo = Uim342Driver::encode_can_id(4, 5, CW_MO, true);
    assert(id_mo == 0x04280095);

    // UIROBOT Manual Page 24 Examples:
    // JV = 0x1D with ACK -> 0x0428009D
    uint32_t id_jv = Uim342Driver::encode_can_id(4, 5, CW_JV, true);
    assert(id_jv == 0x0428009D);

    // BG = 0x16 with ACK -> 0x04280096
    uint32_t id_bg = Uim342Driver::encode_can_id(4, 5, CW_BG, true);
    assert(id_bg == 0x04280096);

    // ST = 0x17 with ACK -> 0x04280097
    uint32_t id_st = Uim342Driver::encode_can_id(4, 5, CW_ST, true);
    assert(id_st == 0x04280097);

    // PR = 0x1F with ACK -> 0x0428009F
    uint32_t id_pr = Uim342Driver::encode_can_id(4, 5, CW_PR, true);
    assert(id_pr == 0x0428009F);

    // PA = 0x20 with ACK -> 0x042800A0
    uint32_t id_pa = Uim342Driver::encode_can_id(4, 5, CW_PA, true);
    assert(id_pa == 0x042800A0);

    // SP = 0x1E with ACK -> 0x0428009E
    uint32_t id_sp = Uim342Driver::encode_can_id(4, 5, CW_SP, true);
    assert(id_sp == 0x0428009E);

    // Test Decoding
    uint8_t prod = 0;
    uint8_t cw = 0;
    bool is_ack = false;
    bool ok = Uim342Driver::decode_can_id(0x04280095, prod, cw, is_ack);
    assert(ok);
    assert(prod == 4);
    assert(cw == 0x95);
    assert(!is_ack); // bit 7 is set, meaning request ACK (not an ACK response)

    printf("  -> CAN-ID Encoding/Decoding PASSED!\n");
}

// ── Test 2: Payload Serialization against UIROBOT Manual ───────────────────
void test_payload_serialization() {
    printf("[TEST] Testing Payload Serialization against UIROBOT Manual...\n");
    MockCan can;
    Uim342Driver driver(can, 5, 4);

    // 1. Jog Velocity: JV = 10000pps -> [0x10, 0x27, 0x00, 0x00] (Page 24)
    // Enqueue matching ACK so driver doesn't block
    hal::CanFrame jv_ack{};
    jv_ack.id = Uim342Driver::encode_can_id(5, 4, CW_JV, false);
    jv_ack.dlc = 5;
    jv_ack.data[0] = 0x02;
    can.rx_queue.push_back(jv_ack);

    // Also ACK for BG
    hal::CanFrame bg_ack{};
    bg_ack.id = Uim342Driver::encode_can_id(5, 4, CW_BG, false);
    bg_ack.dlc = 4;
    can.rx_queue.push_back(bg_ack);

    driver.jog(10000);
    assert(can.transmitted_frames.size() >= 2);
    const auto& f_jv = can.transmitted_frames[0];
    assert(f_jv.id == 0x0428009D);
    assert(f_jv.dlc == 4);
    assert(f_jv.data[0] == 0x10);
    assert(f_jv.data[1] == 0x27);
    assert(f_jv.data[2] == 0x00);
    assert(f_jv.data[3] == 0x00);

    // 2. Jog Velocity: JV = -10000pps -> [0xF0, 0xD8, 0xFF, 0xFF] (Page 24)
    can.clear();
    can.rx_queue.push_back(jv_ack);
    can.rx_queue.push_back(bg_ack);
    driver.jog(-10000);
    assert(can.transmitted_frames.size() >= 2);
    const auto& f_jvn = can.transmitted_frames[0];
    assert(f_jvn.data[0] == 0xF0);
    assert(f_jvn.data[1] == 0xD8);
    assert(f_jvn.data[2] == 0xFF);
    assert(f_jvn.data[3] == 0xFF);

    // 3. Quick Feed PVT Data (Page 97 Example):
    // QP = 10000, QV = -1000, QT = 50ms
    // d0 = 0x32 (50ms)
    // d3:d1 = 0xFFFC18 (-1000)
    // d7:d4 = 0x00002710 (10000)
    can.clear();
    hal::CanFrame qf_ack{};
    qf_ack.id = Uim342Driver::encode_can_id(5, 4, CW_QF, false);
    qf_ack.dlc = 8;
    can.rx_queue.push_back(qf_ack);

    driver.feed_pvt_quick(50, -1000, 10000);
    assert(can.transmitted_frames.size() == 1);
    const auto& f_qf = can.transmitted_frames[0];
    assert(f_qf.id == Uim342Driver::encode_can_id(4, 5, CW_QF, true));
    assert(f_qf.dlc == 8);
    assert(f_qf.data[0] == 0x32);
    assert(f_qf.data[1] == 0x18);
    assert(f_qf.data[2] == 0xFC);
    assert(f_qf.data[3] == 0xFF);
    assert(f_qf.data[4] == 0x10);
    assert(f_qf.data[5] == 0x27);
    assert(f_qf.data[6] == 0x00);
    assert(f_qf.data[7] == 0x00);

    printf("  -> Payload Serialization PASSED!\n");
}

// ── Test 3: Status Queries & Parsing ───────────────────────────────────────
void test_status_queries() {
    printf("[TEST] Testing Status & Diagnostic Decoding...\n");
    MockCan can;
    Uim342Driver driver(can, 5, 4);

    // Prepare MS[0] ACK: Relative Pos = -1000 (0xFFFFFC18), Mode=JOG, Driver=ON
    hal::CanFrame ms0_ack{};
    ms0_ack.id = Uim342Driver::encode_can_id(5, 4, CW_MS, false);
    ms0_ack.dlc = 8;
    ms0_ack.data[0] = 0x00; // Index 0
    ms0_ack.data[1] = 0x3D; // Mode=1 (PTP), SON=1 (Driver ON), IN1=1, IN2=1, IN3=1, OP1=0
    ms0_ack.data[2] = 0x03; // STOP=1, PAIF=1 (In position)
    ms0_ack.data[3] = 0x00;
    ms0_ack.data[4] = 0x18; // PR = -1000
    ms0_ack.data[5] = 0xFC;
    ms0_ack.data[6] = 0xFF;
    ms0_ack.data[7] = 0xFF;
    can.rx_queue.push_back(ms0_ack);

    // Prepare MS[1] ACK: Current Speed = 1000, Absolute Pos = 3200 (0x00000C80)
    hal::CanFrame ms1_ack{};
    ms1_ack.id = Uim342Driver::encode_can_id(5, 4, CW_MS, false);
    ms1_ack.dlc = 8;
    ms1_ack.data[0] = 0x01; // Index 1
    ms1_ack.data[1] = 0xE8; // SP = 1000 (0x0003E8)
    ms1_ack.data[2] = 0x03;
    ms1_ack.data[3] = 0x00;
    ms1_ack.data[4] = 0x80; // PA = 3200 (0x00000C80)
    ms1_ack.data[5] = 0x0C;
    ms1_ack.data[6] = 0x00;
    ms1_ack.data[7] = 0x00;
    can.rx_queue.push_back(ms1_ack);

    UimStatus st{};
    bool ok = driver.query_status(st);
    assert(ok);
    assert(st.driver_on == true);
    assert(st.is_stopped == true);
    assert(st.in_position == true);
    assert(st.relative_position == -1000);
    assert(st.current_speed == 1000);
    assert(st.absolute_position == 3200);

    // Test Temperature (TI) Page 89: 53.1 deg C -> raw 531 = 0x0201
    hal::CanFrame ti_ack{};
    ti_ack.id = Uim342Driver::encode_can_id(5, 4, CW_TI, false);
    ti_ack.dlc = 2;
    ti_ack.data[0] = 0x13; // 531 = 0x0213 -> 53.1 C
    ti_ack.data[1] = 0x02;
    can.rx_queue.push_back(ti_ack);

    float temp = 0.0f;
    assert(driver.query_temperature(temp));
    assert(temp >= 53.0f && temp <= 53.2f);

    printf("  -> Status & Diagnostic Decoding PASSED!\n");
}

// ── Test 4: Asynchronous Real-Time Notifications ───────────────────────────
static bool g_stall_alert_received = false;
static bool g_ptp_done_received = false;

static void test_notification_handler(NotificationType type, uint8_t detail, void* context) {
    (void)context;
    if (type == NotificationType::Alarm && detail == static_cast<uint8_t>(AlarmCode::MotorStallDetected)) {
        g_stall_alert_received = true;
    }
    if (type == NotificationType::PtpPositionCompleted) {
        g_ptp_done_received = true;
    }
}

void test_async_notifications() {
    printf("[TEST] Testing Asynchronous Real-Time Notification (CW 0x5A)...\n");
    MockCan can;
    Uim342Driver driver(can, 5, 4);
    driver.register_notification_callback(test_notification_handler, nullptr);

    // 1. Stall Alert Frame
    hal::CanFrame stall_frame{};
    stall_frame.id = Uim342Driver::encode_can_id(5, 4, CW_RT, false);
    stall_frame.dlc = 5;
    stall_frame.data[0] = 0x00; // Alarm
    stall_frame.data[1] = static_cast<uint8_t>(AlarmCode::MotorStallDetected); // 0x1D
    driver.process_incoming_frame(stall_frame);

    assert(g_stall_alert_received == true);
    assert(driver.get_cached_status().is_stalled == true);

    // 2. PTP Completion Frame
    hal::CanFrame ptp_frame{};
    ptp_frame.id = Uim342Driver::encode_can_id(5, 4, CW_RT, false);
    ptp_frame.dlc = 8;
    ptp_frame.data[0] = static_cast<uint8_t>(NotificationType::PtpPositionCompleted); // 0x29
    ptp_frame.data[4] = 0x80; // Pos = 6400 (0x00001900)
    ptp_frame.data[5] = 0x19;
    ptp_frame.data[6] = 0x00;
    ptp_frame.data[7] = 0x00;
    driver.process_incoming_frame(ptp_frame);

    assert(g_ptp_done_received == true);
    assert(driver.get_cached_status().in_position == true);
    assert(driver.get_cached_status().absolute_position == 6400);

    printf("  -> Asynchronous Real-Time Notification PASSED!\n");
}

// ── Test 5: Generic IMotor Interface Polymorphism ──────────────────────────
void test_imotor_polymorphism() {
    printf("[TEST] Testing hal::IMotor Generic Polymorphism...\n");
    MockCan can;
    Uim342Driver uim_driver(can, 5, 4);
    hal::IMotor* motor = &uim_driver;

    hal::CanFrame mo_ack{};
    mo_ack.id = Uim342Driver::encode_can_id(5, 4, CW_MO, false);
    mo_ack.dlc = 1;
    mo_ack.data[0] = 0x01;
    can.rx_queue.push_back(mo_ack);

    assert(motor->enable());
    assert(motor->is_enabled());

    printf("  -> hal::IMotor Polymorphism PASSED!\n");
}

// ── Test 6: Telemetry Channel Sample Serialization ─────────────────────────
void test_telemetry_channel() {
    printf("[TEST] Testing MotorTelemetryChannel & Sample Serialization...\n");
    net::MotorTelemetryChannel channel(50, 1);
    assert(channel.get_tag() == net::STREAM_TAG_MREG);
    assert(channel.get_sample_rate_hz() == 50);

    net::MotorTelemetrySample sample{};
    sample.current_position = 123456;
    sample.current_velocity = 3200;
    sample.target_position = 200000;
    sample.status_flags = 0x0007;
    sample.temperature_c_x10 = 425; // 42.5 C
    sample.encoder_battery_mv = 3100; // 3.10V

    channel.update_snapshot(sample);
    channel.set_enabled(true);
    assert(channel.is_enabled());

    net::MotorTelemetrySample out_buf[2]{};
    size_t produced = channel.produce_samples(out_buf, 2);
    assert(produced == 2);
    assert(out_buf[0].current_position == 123456);
    assert(out_buf[0].current_velocity == 3200);
    assert(out_buf[0].target_position == 200000);
    assert(out_buf[0].temperature_c_x10 == 425);
    assert(out_buf[0].encoder_battery_mv == 3100);

    printf("  -> MotorTelemetryChannel PASSED!\n");
}

int main() {
    printf("============================================================\n");
    printf("Running UIM342 SimpleCAN 3.0 & Motor Subsystem Unit Tests\n");
    printf("============================================================\n");

    test_can_id_encoding();
    test_payload_serialization();
    test_status_queries();
    test_async_notifications();
    test_imotor_polymorphism();
    test_telemetry_channel();

    printf("============================================================\n");
    printf("ALL MOTOR SUBUNIT UNIT TESTS COMPLETED SUCCESSFULLY (6/6)\n");
    printf("============================================================\n");
    return 0;
}
