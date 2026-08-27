#include "MregCli.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace app::mreg {

MregCli::MregCli(MregSupervisor& supervisor, MregTrajectoryPlanner& planner)
    : supervisor_(supervisor),
      planner_(planner) {}

void MregCli::print_status(char* out, size_t max_len) {
    drivers::uim342::UimStatus st{};
    supervisor_.get_driver().query_status(st);
    float temp_c = 0.0f;
    supervisor_.get_driver().query_temperature(temp_c);
    float v_enc = 0.0f;
    supervisor_.get_driver().query_encoder_voltage(v_enc);

    snprintf(out, max_len,
             "[MREG STATUS]\r\n"
             "  Supervisor State : %s\r\n"
             "  Driver Power     : %s\r\n"
             "  Motion Mode      : %s\r\n"
             "  Abs Position     : %ld pulses\r\n"
             "  Rel Position     : %ld pulses\r\n"
             "  Current Speed    : %ld pps\r\n"
             "  Flags            : Stopped=%d InPos=%d Stall=%d Lock=%d Err=%d\r\n"
             "  Digital I/O      : IN1=%d IN2=%d IN3=%d OP1=%d\r\n"
             "  Core Temp        : %.1f C\r\n"
             "  Encoder Battery  : %.2f V\r\n",
             state_to_string(supervisor_.get_state()),
             st.driver_on ? "ON (Energized)" : "OFF (Freewheel)",
             (st.mode == 0) ? "JOG (Speed)" : "PTP (Position)",
             static_cast<long>(st.absolute_position),
             static_cast<long>(st.relative_position),
             static_cast<long>(st.current_speed),
             st.is_stopped, st.in_position, st.is_stalled, st.is_locked, st.has_error,
             (st.digital_inputs & 1) ? 1 : 0, (st.digital_inputs & 2) ? 1 : 0, (st.digital_inputs & 4) ? 1 : 0, st.digital_output,
             temp_c,
             v_enc);
}

void MregCli::print_help(char* out, size_t max_len) {
    snprintf(out, max_len,
             "MREG Motor Regulation Commands:\r\n"
             "  mreg status                       Query live motor state & diagnostics\r\n"
             "  mreg on                           Energize motor power stage\r\n"
             "  mreg off                          De-energize motor (freewheel)\r\n"
             "  mreg jog <pps>                    Run motor at constant speed\r\n"
             "  mreg move <pos> [speed]           Move to absolute position\r\n"
             "  mreg moverel <dist> [speed]       Move relative distance\r\n"
             "  mreg home                         Set origin / zero coordinate\r\n"
             "  mreg stop                         Decelerate smoothly to stop\r\n"
             "  mreg estop                        Emergency stop and lock\r\n"
             "  mreg clear                        Clear active faults/alarms\r\n"
             "  mreg pvt sine [amp] [freq] [dur]  Run interpolated sine trajectory\r\n"
             "  mreg pvt scurve <target> [speed]  Run interpolated S-curve trajectory\r\n"
             "  mreg current <A_x10> [idle_pct]   Set working & idle current (e.g. 28 50)\r\n"
             "  mreg microsteps <1..128>          Set microstepping resolution\r\n"
             "  mreg raw <cw_hex> [data_hex]      Send raw SimpleCAN instruction\r\n");
}

int MregCli::execute(const char* cmd, char* out_buf, size_t max_len) {
    if (cmd == nullptr || out_buf == nullptr || max_len == 0) {
        return 0;
    }

    const char* p = cmd;
    while (*p == ' ') p++;
    if (strncmp(p, "mreg", 4) == 0) {
        p += 4;
        while (*p == ' ') p++;
    }

    if (*p == '\0' || strcmp(p, "help") == 0) {
        print_help(out_buf, max_len);
        return static_cast<int>(strlen(out_buf));
    }

    if (strcmp(p, "status") == 0) {
        print_status(out_buf, max_len);
        return static_cast<int>(strlen(out_buf));
    }

    if (strcmp(p, "on") == 0) {
        bool ok = supervisor_.power_on();
        snprintf(out_buf, max_len, ok ? "[OK] Motor power stage enabled.\r\n" : "[ERROR] Failed to enable motor.\r\n");
        return static_cast<int>(strlen(out_buf));
    }

    if (strcmp(p, "off") == 0) {
        bool ok = supervisor_.power_off();
        snprintf(out_buf, max_len, ok ? "[OK] Motor power stage disabled.\r\n" : "[ERROR] Failed to disable motor.\r\n");
        return static_cast<int>(strlen(out_buf));
    }

    if (strncmp(p, "jog", 3) == 0) {
        int32_t speed = 3200;
        sscanf(p + 3, "%ld", &speed);
        bool ok = supervisor_.command_jog(speed);
        snprintf(out_buf, max_len, ok ? "[OK] Jogging at %ld pps.\r\n" : "[ERROR] Jog command failed.\r\n", speed);
        return static_cast<int>(strlen(out_buf));
    }

    if (strncmp(p, "move ", 5) == 0) {
        int32_t target = 0;
        int32_t speed = 3200;
        int parsed = sscanf(p + 5, "%ld %ld", &target, &speed);
        if (parsed >= 1) {
            bool ok = supervisor_.command_move_abs(target, static_cast<uint32_t>(speed));
            snprintf(out_buf, max_len, ok ? "[OK] Moving to absolute position %ld (speed %ld).\r\n" : "[ERROR] Move command failed.\r\n", target, speed);
        } else {
            snprintf(out_buf, max_len, "[USAGE] mreg move <target_pulses> [speed_pps]\r\n");
        }
        return static_cast<int>(strlen(out_buf));
    }

    if (strncmp(p, "moverel", 7) == 0) {
        int32_t delta = 0;
        int32_t speed = 3200;
        int parsed = sscanf(p + 7, "%ld %ld", &delta, &speed);
        if (parsed >= 1) {
            bool ok = supervisor_.command_move_rel(delta, static_cast<uint32_t>(speed));
            snprintf(out_buf, max_len, ok ? "[OK] Moving relative distance %ld (speed %ld).\r\n" : "[ERROR] Relative move failed.\r\n", delta, speed);
        } else {
            snprintf(out_buf, max_len, "[USAGE] mreg moverel <delta_pulses> [speed_pps]\r\n");
        }
        return static_cast<int>(strlen(out_buf));
    }

    if (strcmp(p, "home") == 0) {
        bool ok = supervisor_.get_driver().set_origin(0);
        snprintf(out_buf, max_len, ok ? "[OK] Origin coordinate reset to 0.\r\n" : "[ERROR] Set origin failed.\r\n");
        return static_cast<int>(strlen(out_buf));
    }

    if (strcmp(p, "stop") == 0) {
        bool ok = supervisor_.command_stop();
        snprintf(out_buf, max_len, ok ? "[OK] Motor stopped.\r\n" : "[ERROR] Stop failed.\r\n");
        return static_cast<int>(strlen(out_buf));
    }

    if (strcmp(p, "estop") == 0) {
        bool ok = supervisor_.command_emergency_stop();
        snprintf(out_buf, max_len, ok ? "[OK] Emergency stop triggered.\r\n" : "[ERROR] Emergency stop failed.\r\n");
        return static_cast<int>(strlen(out_buf));
    }

    if (strcmp(p, "clear") == 0) {
        bool ok = supervisor_.clear_fault();
        snprintf(out_buf, max_len, ok ? "[OK] Fault flags cleared.\r\n" : "[ERROR] Clear fault failed.\r\n");
        return static_cast<int>(strlen(out_buf));
    }

    if (strncmp(p, "pvt", 3) == 0) {
        char sub[16] = {0};
        sscanf(p + 3, "%s", sub);
        if (strcmp(sub, "sine") == 0) {
            float amp = 3200.0f;
            float freq = 0.5f;
            float dur = 4.0f;
            sscanf(p + 3 + strlen(sub) + 1, "%f %f %f", &amp, &freq, &dur);
            bool ok = planner_.execute_sine_wave(static_cast<int32_t>(amp), freq, dur);
            snprintf(out_buf, max_len, ok ? "[OK] Started PVT sine trajectory (amp=%.0f, freq=%.2fHz, dur=%.1fs).\r\n" : "[ERROR] Failed to start sine PVT.\r\n", amp, freq, dur);
        } else if (strcmp(sub, "scurve") == 0) {
            int32_t target = 6400;
            uint32_t spd = 3200;
            sscanf(p + 3 + strlen(sub) + 1, "%ld %lu", &target, &spd);
            bool ok = planner_.execute_s_curve(target, spd);
            snprintf(out_buf, max_len, ok ? "[OK] Started PVT S-curve trajectory to %ld.\r\n" : "[ERROR] S-curve PVT failed.\r\n", target);
        } else {
            snprintf(out_buf, max_len, "[USAGE] mreg pvt <sine|scurve> [params...]\r\n");
        }
        return static_cast<int>(strlen(out_buf));
    }

    if (strncmp(p, "current", 7) == 0) {
        int working_a = 28;
        int idle_pct = 50;
        sscanf(p + 7, "%d %d", &working_a, &idle_pct);
        bool ok = supervisor_.get_driver().set_drive_current(static_cast<uint8_t>(working_a), static_cast<uint8_t>(idle_pct));
        snprintf(out_buf, max_len, ok ? "[OK] Current configured to %.1fA (idle %d%%).\r\n" : "[ERROR] Failed to set current.\r\n", static_cast<float>(working_a)/10.0f, idle_pct);
        return static_cast<int>(strlen(out_buf));
    }

    if (strncmp(p, "microsteps", 10) == 0) {
        int ms = 16;
        sscanf(p + 10, "%d", &ms);
        bool ok = supervisor_.get_driver().set_microstepping(static_cast<uint8_t>(ms));
        snprintf(out_buf, max_len, ok ? "[OK] Microstepping set to 1/%d.\r\n" : "[ERROR] Failed to set microstepping.\r\n", ms);
        return static_cast<int>(strlen(out_buf));
    }

    snprintf(out_buf, max_len, "[ERROR] Unknown mreg command: '%s'. Type 'mreg help'.\r\n", p);
    return static_cast<int>(strlen(out_buf));
}

} // namespace app::mreg
