#include "board_init.h"
#include "stm32h7rsxx_hal.h"
#include "extmem_manager.h"
#include "console.h"
#include <stdio.h>

// Define the application's base address in external flash
#define APPLICATION_ADDRESS     0x70000000

XSPI_HandleTypeDef hxspi2;
XSPI_HandleTypeDef hxspi1; // Added for EXTMEM macro dependencies

static void MX_XSPI2_Init(void)
{
  XSPIM_CfgTypeDef sXspiManagerCfg = {0};

  /* XSPI2 parameter configuration*/
  hxspi2.Instance = XSPI2;
  hxspi2.Init.FifoThresholdByte = 4;
  hxspi2.Init.MemoryMode = HAL_XSPI_SINGLE_MEM;
  hxspi2.Init.MemoryType = HAL_XSPI_MEMTYPE_MACRONIX;
  hxspi2.Init.MemorySize = HAL_XSPI_SIZE_32GB;
  hxspi2.Init.ChipSelectHighTimeCycle = 2;
  hxspi2.Init.FreeRunningClock = HAL_XSPI_FREERUNCLK_DISABLE;
  hxspi2.Init.ClockMode = HAL_XSPI_CLOCK_MODE_0;
  hxspi2.Init.WrapSize = HAL_XSPI_WRAP_NOT_SUPPORTED;
  hxspi2.Init.ClockPrescaler = 3;
  hxspi2.Init.SampleShifting = HAL_XSPI_SAMPLE_SHIFT_NONE;
  hxspi2.Init.ChipSelectBoundary = HAL_XSPI_BONDARYOF_NONE;
  hxspi2.Init.MaxTran = 0;
  hxspi2.Init.Refresh = 0;
  hxspi2.Init.MemorySelect = HAL_XSPI_CSSEL_NCS1;
  HAL_XSPI_Init(&hxspi2);

  sXspiManagerCfg.nCSOverride = HAL_XSPI_CSSEL_OVR_NCS1;
  sXspiManagerCfg.IOPort = HAL_XSPIM_IOPORT_2;
  sXspiManagerCfg.Req2AckTime = 1U;
  HAL_XSPIM_Config(&hxspi2, &sXspiManagerCfg, HAL_XSPI_TIMEOUT_DEFAULT_VALUE);
}

// Dummy trace for EXTMEM
void EXTMEM_TRACE(uint8_t *Message) {
    (void)Message;
}


typedef void (*pFunction)(void);

int main(void)
{
    // Initialize the HAL and System Clocks
    Board_Init();
    console_init();

    printf("\r\n\r\n=== Bootloader Started ===\r\n");

    // Initialize XSPI hardware
    MX_XSPI2_Init();
    printf("XSPI2 Initialized.\r\n");
    
    // Initialize External Flash in Memory-Mapped Mode via ST EXTMEM Manager
    int32_t extmem_status = MX_EXTMEM_MANAGER_Init();
    if (extmem_status == 0) {
        extmem_status = EXTMEM_MemoryMappedMode(0, EXTMEM_ENABLE);
    }
    printf("EXTMEM_Init status: %ld\r\n", extmem_status);

    if (extmem_status != 0) {
        printf("Failed to map external flash! Halting.\r\n");
        while(1) {}
    }

    printf("External Flash mapped to 0x%08X.\r\n", APPLICATION_ADDRESS);

    printf("Jumping to Application...\r\n");

    // Jump to the application
    uint32_t msp_value = *(__IO uint32_t*)APPLICATION_ADDRESS;
    uint32_t jump_address = *(__IO uint32_t*)(APPLICATION_ADDRESS + 4);
    pFunction JumpToApplication = (pFunction)jump_address;

    printf("Application MSP: 0x%08lX\r\n", msp_value);
    printf("Application Reset Handler: 0x%08lX\r\n", jump_address);

    // Disable SysTick
    SysTick->CTRL = 0;
    SysTick->LOAD = 0;
    SysTick->VAL = 0;

    // Initialize application's Stack Pointer
    __set_MSP(msp_value);

    // Set the Vector Table Offset Register (VTOR)
    SCB->VTOR = APPLICATION_ADDRESS;

    // Disable Caches
    SCB_DisableICache();
    SCB_DisableDCache();

    __DSB();
    __ISB();
    
    // Jump
    JumpToApplication();

    while (1)
    {
        // Should never reach here
    }
}
