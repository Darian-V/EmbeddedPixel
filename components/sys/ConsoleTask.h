#pragma once

#include "Thread.h"
#include "IUart.h"
#include "CliEngine.h"
#include <stdint.h>
#include <stddef.h>

namespace sys {

class ConsoleTask : public osal::Runnable {
public:
    ConsoleTask(hal::IUart& uart, CliEngine& cli);
    ~ConsoleTask() override = default;

    void run() override;

private:
    hal::IUart& uart_;
    CliEngine&  cli_;
    char        line_buffer_[128];
    size_t      buf_pos_;

    void print_prompt();
};

} // namespace sys
