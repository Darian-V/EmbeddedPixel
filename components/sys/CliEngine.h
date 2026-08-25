#pragma once

#include "SystemController.h"
#include <stdint.h>
#include <stddef.h>

namespace sys {

class CliEngine {
public:
    explicit CliEngine(SystemController& sysCtrl);

    /**
     * @brief Execute a text command string and write the result into output_buf.
     * @param input_line Null-terminated ASCII command string
     * @param output_buf Buffer to store command response output
     * @param max_len Maximum length of output_buf
     * @return Length of formatted response text
     */
    int execute(const char* input_line, char* output_buf, size_t max_len);

    /**
     * @brief Execute a command and print output directly to standard stdout / UART.
     * @param input_line Null-terminated ASCII command string
     */
    void execute_interactive(const char* input_line);

private:
    SystemController& sys_;

    void cmd_help(char* out, size_t max_len);
    void cmd_version(char* out, size_t max_len);
    void cmd_status(char* out, size_t max_len);
    void cmd_time(char* out, size_t max_len);
    void cmd_feature(const char* action, const char* name, char* out, size_t max_len);
    void cmd_stream(const char* action, const char* arg1, const char* arg2, char* out, size_t max_len);
    void cmd_ota(const char* action, char* out, size_t max_len);
    void cmd_led(const char* arg, char* out, size_t max_len);
    void cmd_reboot(char* out, size_t max_len);
};

} // namespace sys
