#ifndef BOARD_INIT_H
#define BOARD_INIT_H

#include "IGpio.h"
#include "IUart.h"
#include "ICan.h"

// Interface for board initialization
// Apps can call this without knowing which board is underneath
void Board_Init();

// Returns the board's status LED, fully initialized.
hal::IGpio& Board_GetLed();

// Returns the board's debug UART, fully initialized.
hal::IUart& Board_GetDebugUart();

// Returns the board's CAN controller, fully initialized.
hal::ICan& Board_GetCan();

#endif // BOARD_INIT_H
