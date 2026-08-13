#ifndef CONSOLE_H
#define CONSOLE_H

#include "IUart.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Initialize console output with a UART backend.
 * @param uart Reference to an initialized IUart implementation.
 *             Must outlive console usage (typically static/global).
 *
 * After calling this, printf/puts will route through the given UART.
 */
void console_init(hal::IUart& uart);

#ifdef __cplusplus
}
#endif

#endif // CONSOLE_H
