#include "board_init.h"
#include "stm32h7rsxx_hal.h"
#include "console.h"
#include "BlinkTask.h"
#include "FreeRtosThread.h"
#include "Uim342Driver.h"
#include "MregSupervisor.h"
#include "MregTrajectoryPlanner.h"
#include "MregCli.h"
#include "MotorTelemetryChannel.h"

#include <stdio.h>
#include <cstring>

// ── Background CAN Receiver & Dispatcher Task ──────────────────────────────
class CanRxTask : public osal::Runnable {
public:
    CanRxTask(hal::ICan& can, drivers::uim342::Uim342Driver& driver)
        : can_(can), driver_(driver) {}

    void run() override {
        while (1) {
            hal::CanFrame rx_frame{};
            if (can_.receive(rx_frame, 5)) {
                driver_.process_incoming_frame(rx_frame);
            }
            osal::Thread::delay(2);
        }
    }

private:
    hal::ICan&                     can_;
    drivers::uim342::Uim342Driver& driver_;
};

// ── Motion Supervisory & Regulation Loop Task ──────────────────────────────
class MregControlTask : public osal::Runnable {
public:
    MregControlTask(app::mreg::MregSupervisor& supervisor, net::MotorTelemetryChannel& telem)
        : supervisor_(supervisor), telem_(telem) {}

    void run() override {
        supervisor_.init();

        while (1) {
            supervisor_.update();

            // Snapshot latest metrics for optional telemetry stream
            const auto& st = supervisor_.get_status();
            net::MotorTelemetrySample sample{};
            sample.current_position = st.absolute_position;
            sample.current_velocity = st.current_speed;
            sample.target_position = 0;
            sample.status_flags = (st.driver_on ? 1 : 0) |
                                  (st.is_stopped ? (1 << 1) : 0) |
                                  (st.in_position ? (1 << 2) : 0) |
                                  (st.is_stalled ? (1 << 3) : 0);
            sample.temperature_c_x10 = static_cast<int16_t>(st.temperature_c * 10.0f);
            sample.encoder_battery_mv = static_cast<uint16_t>(st.encoder_battery_v * 1000.0f);
            telem_.update_snapshot(sample);

            osal::Thread::delay(20); // 50 Hz loop rate
        }
    }

private:
    app::mreg::MregSupervisor& supervisor_;
    net::MotorTelemetryChannel& telem_;
};

// ── Interactive Console Terminal Task ──────────────────────────────────────
class MregConsoleTask : public osal::Runnable {
public:
    MregConsoleTask(hal::IUart& uart, app::mreg::MregCli& cli)
        : uart_(uart), cli_(cli), buf_pos_(0) {
        std::memset(line_buf_, 0, sizeof(line_buf_));
    }

    void run() override {
        print_prompt();

        while (1) {
            uint8_t ch = 0;
            if (uart_.receive_byte(ch, 10)) {
                if (ch == '\r' || ch == '\n') {
                    uart_.transmit(reinterpret_cast<const uint8_t*>("\r\n"), 2);
                    if (buf_pos_ > 0) {
                        line_buf_[buf_pos_] = '\0';
                        char resp[512]{0};
                        int len = cli_.execute(line_buf_, resp, sizeof(resp));
                        if (len > 0) {
                            uart_.transmit(reinterpret_cast<const uint8_t*>(resp), len);
                        }
                        buf_pos_ = 0;
                    }
                    print_prompt();
                } else if (ch == '\b' || ch == 0x7F) {
                    if (buf_pos_ > 0) {
                        buf_pos_--;
                        uart_.transmit(reinterpret_cast<const uint8_t*>("\b \b"), 3);
                    }
                } else if (buf_pos_ < sizeof(line_buf_) - 1 && ch >= 32 && ch <= 126) {
                    line_buf_[buf_pos_++] = static_cast<char>(ch);
                    uart_.transmit(&ch, 1); // Local echo
                }
            }
            osal::Thread::delay(5);
        }
    }

private:
    hal::IUart&       uart_;
    app::mreg::MregCli& cli_;
    char              line_buf_[128];
    size_t            buf_pos_;

    void print_prompt() {
        const char prompt[] = "MREG> ";
        uart_.transmit(reinterpret_cast<const uint8_t*>(prompt), sizeof(prompt) - 1);
    }
};

int main(void) {
    // 1. Board Hardware Initialization
    Board_Init();
    console_init(Board_GetDebugUart());

    printf("\r\n============================================================\r\n");
    printf(" EmbeddedPixel MREG: Motor Regulation Application Started   \r\n");
    printf(" Target: Nucleo-H7S3L8 | CAN: FDCAN1 @ 500kbps (SimpleCAN3.0)\r\n");
    printf(" Default Node ID: 5 | Master Producer ID: 4                 \r\n");
    printf("============================================================\r\n\r\n");

    // 2. Initialize CAN Bus
    hal::ICan& can_bus = Board_GetCan();
    if (!can_bus.init(hal::CanBaudRate::Baud500k)) {
        printf("[ERROR] Failed to initialize CAN controller!\r\n");
    } else {
        printf("[OK] CAN Controller initialized at 500 kbps.\r\n");
    }

    // 3. Instantiate Architecture Components
    static hal::IGpio& status_led = Board_GetGreenLed();
    static app::BlinkTask blinky(status_led, 500);

    static drivers::uim342::Uim342Driver motor_driver(can_bus, 5, 4);
    static app::mreg::MregSupervisor supervisor(motor_driver);
    static app::mreg::MregTrajectoryPlanner planner(motor_driver);
    static app::mreg::MregCli mreg_cli(supervisor, planner);
    static net::MotorTelemetryChannel telem_channel(50, 1);

    // 4. Instantiate FreeRTOS Tasks
    static CanRxTask can_rx_task(can_bus, motor_driver);
    static MregControlTask mreg_ctrl_task(supervisor, telem_channel);
    static MregConsoleTask console_task(Board_GetDebugUart(), mreg_cli);

    static stm32::FreeRtosThread blink_thread(blinky, "BlinkTask", 256, 1);
    static stm32::FreeRtosThread can_rx_thread(can_rx_task, "CanRxTask", 1024, 4);
    static stm32::FreeRtosThread mreg_thread(mreg_ctrl_task, "MregTask", 1024, 3);
    static stm32::FreeRtosThread console_thread(console_task, "ConsoleCLI", 1024, 2);

    // 5. Start Threads & FreeRTOS Scheduler
    blink_thread.start();
    can_rx_thread.start();
    mreg_thread.start();
    console_thread.start();

    vTaskStartScheduler();

    while (1) {}
    return 0;
}
