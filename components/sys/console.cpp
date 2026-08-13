#include "console.h"
#include <stdio.h>

static hal::IUart* g_console_uart = nullptr;

void console_init(hal::IUart& uart)
{
    g_console_uart = &uart;
}

extern "C" {

// Redirect standard output through the registered UART
int _write(int file, char *ptr, int len)
{
    (void)file;
    if (g_console_uart) {
        g_console_uart->transmit((const uint8_t*)ptr, (size_t)len);
    }
    return len;
}

} // extern "C"
