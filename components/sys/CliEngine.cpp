#include "CliEngine.h"
#include "Version.h"
#include "proto/ProtocolTypes.h"
#include "FreeRTOS.h"
#include "task.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

namespace sys {

CliEngine::CliEngine(SystemController& sysCtrl)
    : sys_(sysCtrl) {
}

int CliEngine::execute(const char* input_line, char* output_buf, size_t max_len) {
    if (output_buf == nullptr || max_len == 0) return 0;
    output_buf[0] = '\0';

    if (input_line == nullptr || input_line[0] == '\0') {
        return 0;
    }

    char line_copy[128];
    strncpy(line_copy, input_line, sizeof(line_copy) - 1);
    line_copy[sizeof(line_copy) - 1] = '\0';

    // Strip CR/LF
    char* end = line_copy + strlen(line_copy) - 1;
    while (end >= line_copy && (*end == '\r' || *end == '\n' || *end == ' ')) {
        *end = '\0';
        end--;
    }

    // Skip leading whitespace
    char* cur = line_copy;
    while (*cur == ' ' || *cur == '\t') cur++;

    if (*cur == '\0') {
        return 0;
    }

    char* saveptr = nullptr;
    char* cmd = strtok_r(cur, " ", &saveptr);
    if (cmd == nullptr) return 0;

    if (strcmp(cmd, "help") == 0 || strcmp(cmd, "?") == 0) {
        cmd_help(output_buf, max_len);
    } else if (strcmp(cmd, "version") == 0 || strcmp(cmd, "info") == 0) {
        cmd_version(output_buf, max_len);
    } else if (strcmp(cmd, "status") == 0) {
        cmd_status(output_buf, max_len);
    } else if (strcmp(cmd, "time") == 0) {
        cmd_time(output_buf, max_len);
    } else if (strcmp(cmd, "feature") == 0) {
        char* action = strtok_r(nullptr, " ", &saveptr);
        char* name   = strtok_r(nullptr, " ", &saveptr);
        cmd_feature(action, name, output_buf, max_len);
    } else if (strcmp(cmd, "stream") == 0) {
        char* action = strtok_r(nullptr, " ", &saveptr);
        char* arg1   = strtok_r(nullptr, " ", &saveptr);
        char* arg2   = strtok_r(nullptr, " ", &saveptr);
        cmd_stream(action, arg1, arg2, output_buf, max_len);
    } else if (strcmp(cmd, "ota") == 0) {
        char* action = strtok_r(nullptr, " ", &saveptr);
        cmd_ota(action, output_buf, max_len);
    } else if (strcmp(cmd, "led") == 0) {
        char* arg = strtok_r(nullptr, " ", &saveptr);
        cmd_led(arg, output_buf, max_len);
    } else if (strcmp(cmd, "reboot") == 0 || strcmp(cmd, "reset") == 0) {
        cmd_reboot(output_buf, max_len);
    } else {
        snprintf(output_buf, max_len, "ERR: Unknown command '%s'. Type 'help' for usage.\r\n", cmd);
    }

    return static_cast<int>(strlen(output_buf));
}

void CliEngine::execute_interactive(const char* input_line) {
    char out[512];
    int len = execute(input_line, out, sizeof(out));
    if (len > 0) {
        printf("%s", out);
    }
}

void CliEngine::cmd_help(char* out, size_t max_len) {
    snprintf(out, max_len,
        "=== EmbeddedPixel Command Line Interface ===\r\n"
        "  help / ?                   Show available commands\r\n"
        "  version / info             Show firmware, bootloader & board info\r\n"
        "  status                     Show system health, telemetry & IP status\r\n"
        "  feature list               Show active/inactive feature flags\r\n"
        "  feature enable <name>      Enable feature (ota, telemetry, dts, cli)\r\n"
        "  feature disable <name>     Disable feature (e.g. lock OTA updates)\r\n"
        "  stream start [tag] [rate]  Start streaming (e.g. stream start CNTR 50)\r\n"
        "  stream stop [tag]          Stop streaming\r\n"
        "  ota status                 Display OTA update staging status\r\n"
        "  ota abort                  Cancel pending OTA update\r\n"
        "  led <period_ms>            Configure LED blink period\r\n"
        "  reboot / reset             Reboot node into bootloader\r\n"
    );
}

void CliEngine::cmd_version(char* out, size_t max_len) {
    char app_ver_str[24];
    char bl_ver_str[24];
    format_version(sys_.get_app_version(), app_ver_str, sizeof(app_ver_str));
    format_version(sys_.get_bootloader_version(), bl_ver_str, sizeof(bl_ver_str));

    snprintf(out, max_len,
        "=== Node Version Information ===\r\n"
        "  Board:              %s (0x%04X)\r\n"
        "  Node ID:            %u\r\n"
        "  App Firmware:       %s (0x%08lX)\r\n"
        "  Bootloader:         %s (0x%08lX)\r\n"
        "  Git Commit:         0x%08lX\r\n"
        "  Active Features:    0x%08lX\r\n",
        sys_.get_board_name(),
        sys_.get_board_id(),
        sys_.get_node_id(),
        app_ver_str,
        sys_.get_app_version(),
        bl_ver_str,
        sys_.get_bootloader_version(),
        sys_.get_git_commit(),
        sys_.get_feature_flags()
    );
}

void CliEngine::cmd_status(char* out, size_t max_len) {
    uint32_t ip = sys_.get_ip_addr();
    uint32_t uptime_s = sys_.get_uptime_ms() / 1000;
    float temp = sys_.get_core_temp_c();
    int temp_whole = static_cast<int>(temp);
    int temp_frac = static_cast<int>((temp - temp_whole) * 10.0f);
    if (temp_frac < 0) temp_frac = -temp_frac;
    bool ota_on = sys_.is_feature_enabled(FeatureFlag::FEAT_OTA_RAM_STAGING);
    bool stream_on = sys_.is_streaming();

    snprintf(out, max_len,
        "=== System Health & Status ===\r\n"
        "  IP Address:    %u.%u.%u.%u\r\n"
        "  Uptime:        %lu seconds (%lu ms)\r\n"
        "  Core Temp:     %d.%d C\r\n"
        "  LED Period:    %lu ms\r\n"
        "  Telemetry:     %s\r\n"
        "  OTA Updates:   %s\r\n",
        (unsigned)(ip & 0xFF),
        (unsigned)((ip >> 8) & 0xFF),
        (unsigned)((ip >> 16) & 0xFF),
        (unsigned)((ip >> 24) & 0xFF),
        (unsigned long)uptime_s,
        (unsigned long)sys_.get_uptime_ms(),
        (int)temp_whole,
        (int)temp_frac,
        (unsigned long)sys_.get_blink_rate(),
        stream_on ? "STREAMING ACTIVE" : "IDLE",
        ota_on ? "ENABLED (Unlocked)" : "DISABLED (Locked)"
    );
}

void CliEngine::cmd_time(char* out, size_t max_len) {
    uint32_t uptime_ms = sys_.get_uptime_ms();
    uint32_t uptime_s = uptime_ms / 1000;
    snprintf(out, max_len,
        "=== Node System Time ===\r\n"
        "  Uptime:             %lu seconds (%lu ms)\r\n"
        "  Tick Count:         %lu ticks\r\n",
        uptime_s,
        uptime_ms,
        (unsigned long)xTaskGetTickCount()
    );
}

void CliEngine::cmd_feature(const char* action, const char* name, char* out, size_t max_len) {
    if (action == nullptr || strcmp(action, "list") == 0) {
        uint32_t mask = sys_.get_feature_flags();
        snprintf(out, max_len,
            "=== Feature Flags Configuration (0x%08lX) ===\r\n"
            "  ota          [0x%04X] : %s\r\n"
            "  telemetry    [0x%04X] : %s\r\n"
            "  dts          [0x%04X] : %s\r\n"
            "  ethernet     [0x%04X] : %s\r\n"
            "  dynrate      [0x%04X] : %s\r\n"
            "  cli          [0x%04X] : %s\r\n",
            (unsigned long)mask,
            (unsigned)FeatureFlag::FEAT_OTA_RAM_STAGING,  is_feature_set(mask, FeatureFlag::FEAT_OTA_RAM_STAGING)  ? "ENABLED" : "DISABLED",
            (unsigned)FeatureFlag::FEAT_TELEMETRY_STREAM, is_feature_set(mask, FeatureFlag::FEAT_TELEMETRY_STREAM) ? "ENABLED" : "DISABLED",
            (unsigned)FeatureFlag::FEAT_TEMP_SENSOR_DTS,  is_feature_set(mask, FeatureFlag::FEAT_TEMP_SENSOR_DTS)  ? "ENABLED" : "DISABLED",
            (unsigned)FeatureFlag::FEAT_ETHERNET_LAN8742, is_feature_set(mask, FeatureFlag::FEAT_ETHERNET_LAN8742) ? "ENABLED" : "DISABLED",
            (unsigned)FeatureFlag::FEAT_DYNAMIC_RATE,     is_feature_set(mask, FeatureFlag::FEAT_DYNAMIC_RATE)     ? "ENABLED" : "DISABLED",
            (unsigned)FeatureFlag::FEAT_UART_CLI,         is_feature_set(mask, FeatureFlag::FEAT_UART_CLI)         ? "ENABLED" : "DISABLED"
        );
        return;
    }

    if (name == nullptr) {
        snprintf(out, max_len, "ERR: Missing feature name. Example: feature enable ota\r\n");
        return;
    }

    FeatureFlag flag = parse_feature_name(name);
    if (flag == FeatureFlag::NONE) {
        snprintf(out, max_len, "ERR: Unknown feature '%s'. Supported: ota, telemetry, dts, ethernet, dynrate, cli\r\n", name);
        return;
    }

    if (strcmp(action, "enable") == 0) {
        sys_.set_feature(flag, true);
        snprintf(out, max_len, "OK: Feature '%s' is now ENABLED [Flags: 0x%08lX]\r\n", name, sys_.get_feature_flags());
    } else if (strcmp(action, "disable") == 0) {
        sys_.set_feature(flag, false);
        snprintf(out, max_len, "OK: Feature '%s' is now DISABLED [Flags: 0x%08lX]\r\n", name, sys_.get_feature_flags());
    } else {
        snprintf(out, max_len, "ERR: Unknown action '%s'. Use 'enable', 'disable', or 'list'.\r\n", action);
    }
}

void CliEngine::cmd_stream(const char* action, const char* arg1, const char* arg2, char* out, size_t max_len) {
    if (action == nullptr || strcmp(action, "status") == 0) {
        snprintf(out, max_len, "Streaming is %s\r\n", sys_.is_streaming() ? "ACTIVE" : "STOPPED");
        return;
    }

    if (strcmp(action, "start") == 0) {
        uint32_t tag = 0;
        uint16_t rate = 0;
        if (arg1 != nullptr) {
            if (strlen(arg1) == 4) {
                tag = net::proto::MAKE_FOURCC(arg1[0], arg1[1], arg1[2], arg1[3]);
            } else {
                rate = static_cast<uint16_t>(atoi(arg1));
            }
        }
        if (arg2 != nullptr) {
            rate = static_cast<uint16_t>(atoi(arg2));
        }

        if (sys_.start_telemetry(tag, rate)) {
            snprintf(out, max_len, "OK: Telemetry streaming started (tag=0x%08lX, rate=%u Hz)\r\n", tag, rate);
        } else {
            snprintf(out, max_len, "ERR: Failed to start telemetry (Feature disabled or service unavailable)\r\n");
        }
    } else if (strcmp(action, "stop") == 0) {
        uint32_t tag = 0;
        if (arg1 != nullptr && strlen(arg1) == 4) {
            tag = net::proto::MAKE_FOURCC(arg1[0], arg1[1], arg1[2], arg1[3]);
        }
        sys_.stop_telemetry(tag);
        snprintf(out, max_len, "OK: Telemetry streaming stopped\r\n");
    } else {
        snprintf(out, max_len, "ERR: Usage: stream start [TAG] [RATE_HZ] | stream stop\r\n");
    }
}

void CliEngine::cmd_ota(const char* action, char* out, size_t max_len) {
    if (action == nullptr || strcmp(action, "status") == 0) {
        bool enabled = sys_.is_feature_enabled(FeatureFlag::FEAT_OTA_RAM_STAGING);
        snprintf(out, max_len, "OTA Update Service: %s\r\n", enabled ? "ENABLED (Ready)" : "LOCKED / DISABLED");
    } else if (strcmp(action, "abort") == 0) {
        sys_.abort_ota();
        snprintf(out, max_len, "OK: Active OTA update aborted.\r\n");
    } else {
        snprintf(out, max_len, "ERR: Usage: ota status | ota abort\r\n");
    }
}

void CliEngine::cmd_led(const char* arg, char* out, size_t max_len) {
    if (arg == nullptr) {
        snprintf(out, max_len, "Current LED Blink Period: %lu ms\r\n", sys_.get_blink_rate());
        return;
    }
    int period = atoi(arg);
    if (period < 10 || period > 10000) {
        snprintf(out, max_len, "ERR: Invalid period %d ms. Must be between 10 and 10000 ms.\r\n", period);
        return;
    }
    sys_.set_blink_rate(static_cast<uint32_t>(period));
    snprintf(out, max_len, "OK: LED blink period set to %d ms\r\n", period);
}

void CliEngine::cmd_reboot(char* out, size_t max_len) {
    snprintf(out, max_len, "Rebooting system into Bootloader...\r\n");
    sys_.reboot();
}

} // namespace sys
