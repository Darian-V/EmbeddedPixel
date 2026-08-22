#include "ConsoleTask.h"
#include "FreeRTOS.h"
#include "task.h"
#include <stdio.h>
#include <string.h>

namespace sys {

ConsoleTask::ConsoleTask(hal::IUart& uart, CliEngine& cli)
    : uart_(uart), cli_(cli), buf_pos_(0) {
    memset(line_buffer_, 0, sizeof(line_buffer_));
}

void ConsoleTask::print_prompt() {
    printf("EmbeddedPixel> ");
    fflush(stdout);
}

void ConsoleTask::run() {
    // Small delay to allow other system components & banners to initialize
    vTaskDelay(pdMS_TO_TICKS(500));

    printf("\r\n========================================\r\n");
    printf(" EmbeddedPixel Interactive CLI Ready\r\n");
    printf(" Type 'help' or '?' for available commands.\r\n");
    printf("========================================\r\n");
    print_prompt();

    uint8_t rx_byte = 0;

    while (true) {
        // Read 1 byte with 50ms timeout to allow thread scheduling
        if (uart_.receive_byte(rx_byte, 50)) {
            if (rx_byte == '\r' || rx_byte == '\n') {
                printf("\r\n");
                line_buffer_[buf_pos_] = '\0';

                if (buf_pos_ > 0) {
                    cli_.execute_interactive(line_buffer_);
                    buf_pos_ = 0;
                    memset(line_buffer_, 0, sizeof(line_buffer_));
                }

                print_prompt();
            } else if (rx_byte == '\b' || rx_byte == 0x7F) {
                if (buf_pos_ > 0) {
                    buf_pos_--;
                    line_buffer_[buf_pos_] = '\0';
                    printf("\b \b");
                    fflush(stdout);
                }
            } else if (rx_byte >= 32 && rx_byte <= 126) {
                if (buf_pos_ < sizeof(line_buffer_) - 1) {
                    line_buffer_[buf_pos_++] = static_cast<char>(rx_byte);
                    line_buffer_[buf_pos_] = '\0';
                    putchar(rx_byte);
                    fflush(stdout);
                }
            }
        } else {
            // No character received in 50ms, task naturally yields
        }
    }
}

} // namespace sys
