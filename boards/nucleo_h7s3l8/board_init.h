#ifndef BOARD_INIT_H
#define BOARD_INIT_H

#include "IGpio.h"

// Interface for board initialization
// Apps can call this without knowing which board is underneath
void Board_Init();

// Returns the board's status LED, fully initialized.
hal::IGpio& Board_GetLed();

#endif // BOARD_INIT_H
