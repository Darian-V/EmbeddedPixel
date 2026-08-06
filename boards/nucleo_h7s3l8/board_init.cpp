#include "board_init.h"
#include "stm32h7rsxx_hal.h"

extern "C" void Error_Handler(void);

// Board-specific clock configuration
static void SystemClock_Config(void) {
    // For initial bringup of Nucleo-H7S3L8, we bypass complex clock 
    // configurations (SMPS vs LDO mismatch risks) and boot safely 
    // from the internal 64MHz HSI.
    
    // Once the board needs higher performance, implement the specific
    // PLL and Power supply configuration here.
}

void Board_Init() {
    // 1. Initialize the ST HAL
    HAL_Init();

    // 2. Configure the System Clock
    SystemClock_Config();
}
