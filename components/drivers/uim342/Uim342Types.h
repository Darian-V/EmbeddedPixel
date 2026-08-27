#pragma once

#include <cstdint>
#include <cstddef>

namespace drivers::uim342 {

// ── SimpleCAN 3.0 Control Words (CW) ───────────────────────────────────────
constexpr uint8_t CW_PP = 0x01; ///< Protocol Parameters
constexpr uint8_t CW_IC = 0x06; ///< Initial Configuration
constexpr uint8_t CW_IE = 0x07; ///< Information Enable
constexpr uint8_t CW_ML = 0x0B; ///< Model & Firmware Version
constexpr uint8_t CW_SN = 0x0C; ///< Serial Number
constexpr uint8_t CW_ER = 0x0F; ///< Error Report
constexpr uint8_t CW_MT = 0x10; ///< Motor Driver Parameters
constexpr uint8_t CW_MS = 0x11; ///< Motion Status
constexpr uint8_t CW_MO = 0x15; ///< Motor Driver On / Off
constexpr uint8_t CW_BG = 0x16; ///< Begin Motion
constexpr uint8_t CW_ST = 0x17; ///< Stop Motion
constexpr uint8_t CW_MF = 0x18; ///< Motion Parameter Frame
constexpr uint8_t CW_AC = 0x19; ///< Acceleration
constexpr uint8_t CW_DC = 0x1A; ///< Deceleration
constexpr uint8_t CW_SS = 0x1B; ///< Cut-in Speed
constexpr uint8_t CW_SD = 0x1C; ///< Stop Deceleration
constexpr uint8_t CW_JV = 0x1D; ///< Jog Velocity
constexpr uint8_t CW_SP = 0x1E; ///< PTP Speed
constexpr uint8_t CW_PR = 0x1F; ///< Position Relative
constexpr uint8_t CW_PA = 0x20; ///< Position Absolute
constexpr uint8_t CW_OG = 0x21; ///< Set Origin
constexpr uint8_t CW_MP = 0x22; ///< PVT/PT Motion Parameters
constexpr uint8_t CW_PV = 0x23; ///< Set PVT Motion & Starting Row
constexpr uint8_t CW_PT = 0x24; ///< Set PT Data
constexpr uint8_t CW_QP = 0x25; ///< Queued Position
constexpr uint8_t CW_QV = 0x26; ///< Queued Velocity
constexpr uint8_t CW_QT = 0x27; ///< Queued Time Interval
constexpr uint8_t CW_QF = 0x29; ///< Quick Feed PVT Queue
constexpr uint8_t CW_LM = 0x2C; ///< Software Limits
constexpr uint8_t CW_BL = 0x2D; ///< Backlash Compensation
constexpr uint8_t CW_DV = 0x2E; ///< Desired Values
constexpr uint8_t CW_IL = 0x34; ///< Input Logic
constexpr uint8_t CW_TG = 0x35; ///< Trigger Mode
constexpr uint8_t CW_DI = 0x37; ///< Digital I/O
constexpr uint8_t CW_QE = 0x3D; ///< Quadrature Encoder
constexpr uint8_t CW_RT = 0x5A; ///< Real-Time Inform (Auto Notification)
constexpr uint8_t CW_SY = 0x7E; ///< System Operation
constexpr uint8_t CW_TI = 0xBA; ///< Temperature Indication

constexpr uint8_t FLAG_ACK_REQUEST = 0x80;

// ── Real-Time Notification Types (CW = 0x5A) ───────────────────────────────
enum class NotificationType : uint8_t {
    Alarm                = 0x00,
    Input1FallingEdge    = 0x01,
    Input1RisingEdge     = 0x02,
    Input2FallingEdge    = 0x03,
    Input2RisingEdge     = 0x04,
    Input3FallingEdge    = 0x05,
    Input3RisingEdge     = 0x06,
    PtpPositionCompleted = 0x29,
};

enum class AlarmCode : uint8_t {
    EmergencyStopLock   = 0x0A,
    AccDecOverLimit     = 0x16,
    SpeedOverLimit      = 0x17,
    ExceedLowerBumping  = 0x18,
    ExceedUpperBumping  = 0x19,
    ExceedLowerWorking  = 0x1A,
    ExceedUpperWorking  = 0x1B,
    MotorStallDetected  = 0x1D,
    EncoderError        = 0x1E,
    EncoderBatteryLow   = 0x1F,
};

// ── PVT Motion Modes ───────────────────────────────────────────────────────
enum class PvtMode : uint16_t {
    FIFO   = 0,
    Single = 1,
    Loop   = 3,
};

// ── UIM342 Motion Status Bitfield (ACK for MS[0]) ──────────────────────────
#pragma pack(push, 1)
struct MotionStatusByte1 {
    uint8_t mode : 2; ///< 0=JOG, 1=PTP
    uint8_t son  : 1; ///< 0=OFF, 1=ON (motor driver)
    uint8_t in1  : 1; ///< IN1 logic level
    uint8_t in2  : 1; ///< IN2 logic level
    uint8_t in3  : 1; ///< IN3 logic level
    uint8_t op1  : 1; ///< OP1 logic level
    uint8_t res  : 1;
};

struct MotionStatusByte2 {
    uint8_t stop : 1; ///< 1=Motor is stationary
    uint8_t paif : 1; ///< 1=Motor is in position
    uint8_t psif : 1; ///< 1=PVT Stopped
    uint8_t tlif : 1; ///< 1=Motor stall detected
    uint8_t res1 : 1;
    uint8_t lock : 1; ///< 1=System is locked down
    uint8_t res2 : 1;
    uint8_t err  : 1; ///< 1=System error detected
};

struct UimStatus {
    bool     driver_on{false};
    bool     is_stopped{false};
    bool     in_position{false};
    bool     is_stalled{false};
    bool     is_locked{false};
    bool     has_error{false};
    uint8_t  mode{0};               ///< 0=JOG, 1=PTP
    uint8_t  digital_inputs{0};     ///< Bit 0=IN1, Bit 1=IN2, Bit 2=IN3
    uint8_t  digital_output{0};     ///< Bit 0=OP1
    int32_t  relative_position{0};  ///< Pulses
    int32_t  absolute_position{0};  ///< Pulses
    int32_t  current_speed{0};      ///< Pulses/sec
    float    temperature_c{0.0f};   ///< Controller temp in deg C
    float    encoder_battery_v{0.0f};///< Encoder battery in Volts
};

struct UimErrorInfo {
    uint8_t error_code{0};
    uint8_t related_cw{0};
    uint8_t sub_index{0};
};

struct UimModelInfo {
    uint8_t  model_code[4]{0};
    uint16_t fw_version{0};
    uint32_t serial_number{0};
};
#pragma pack(pop)

} // namespace drivers::uim342
