#ifndef BOARD_INIT_H
#define BOARD_INIT_H

#include "IGpio.h"
#include "IUart.h"
#include "ITempSensor.h"

// Interface for board initialization
// Apps can call this without knowing which board is underneath
void Board_Init();

// Returns the board's status LED, fully initialized.
hal::IGpio& Board_GetLed();
hal::IGpio& Board_GetGreenLed();
hal::IGpio& Board_GetRedLed();
hal::IGpio& Board_GetYellowLed();

// Returns the board's debug UART, fully initialized.
hal::IUart& Board_GetDebugUart();

// Returns the board's on-chip temperature sensor.
hal::ITempSensor& Board_GetTempSensor();

#endif // BOARD_INIT_H
