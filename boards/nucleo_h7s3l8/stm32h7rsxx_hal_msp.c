/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file    stm32h7rsxx_hal_msp.c
  * @brief        This file provides code for the MSP Initialization
  *               and de-Initialization codes.
  ******************************************************************************
  * @attention
  *
  * Copyright (c) 2023 STMicroelectronics.
  * All rights reserved.
  *
  * This software is licensed under terms that can be found in the LICENSE file
  * in the root directory of this software component.
  * If no LICENSE file comes with this software, it is provided AS-IS.
  *
  ******************************************************************************
  */
/* USER CODE END Header */

#include "stm32h7rsxx_hal.h"
extern void Error_Handler(void);

/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN TD */

/* USER CODE END TD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN Define */

/* USER CODE END Define */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN Macro */

/* USER CODE END Macro */

/* Private variables ---------------------------------------------------------*/
/* USER CODE BEGIN PV */

/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
/* USER CODE BEGIN PFP */

/* USER CODE END PFP */

/* External functions --------------------------------------------------------*/
/* USER CODE BEGIN ExternalFunctions */

/* USER CODE END ExternalFunctions */

/* USER CODE BEGIN 0 */

/* USER CODE END 0 */
/**
  * Initializes the Global MSP.
  */
void HAL_MspInit(void)
{
#ifdef BOOTLOADER
  RCC_OscInitTypeDef RCC_OscInitStruct = {0};

  /* Configure the system Power Supply */

  if (HAL_PWREx_ConfigSupply(PWR_LDO_SUPPLY) != HAL_OK)
  {
    /* Initialization error */
    Error_Handler();
  }

  /* USER CODE BEGIN MspInit 0 */

  /* USER CODE END MspInit 0 */

  __HAL_RCC_SBS_CLK_ENABLE();

  /* System interrupt init*/

  /* Enable the XSPIM_P2 interface */
  HAL_PWREx_EnableXSPIM2();

  /* The CSI is used by the compensation cells and must be enabled before enabling the
     compensation cells.
     For more details refer to RM0477 [SBS I/O compensation cell management] chapter.
  */
  RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_CSI;
  RCC_OscInitStruct.CSIState = RCC_CSI_ON;
  if (HAL_RCC_OscConfig(&RCC_OscInitStruct) != HAL_OK)
  {
    Error_Handler();
  }

  /* Configure the compensation cell */
  HAL_SBS_ConfigCompensationCell(SBS_IO_XSPI2_CELL, SBS_IO_CELL_CODE, 0U, 0U);

  /* Enable compensation cell */
  HAL_SBS_EnableCompensationCell(SBS_IO_XSPI2_CELL);

  /* wait ready before enabled IO */
  while(HAL_SBS_GetCompensationCellReadyStatus(SBS_IO_XSPI2_CELL_READY) != 1U);

  /* high speed low voltage config */
  HAL_SBS_EnableIOSpeedOptimize(SBS_IO_XSPI2_HSLV);

  /* USER CODE BEGIN MspInit 1 */

  /* USER CODE END MspInit 1 */
#endif
}

/**
* @brief XSPI MSP Initialization
* This function configures the hardware resources used in this example
* @param hxspi: XSPI handle pointer
* @retval None
*/
void HAL_XSPI_MspInit(XSPI_HandleTypeDef* hxspi)
{
#ifdef BOOTLOADER
  GPIO_InitTypeDef GPIO_InitStruct = {0};
  RCC_PeriphCLKInitTypeDef PeriphClkInit = {0};
  if(hxspi->Instance==XSPI2)
  {
  /* USER CODE BEGIN XSPI2_MspInit 0 */

  /* USER CODE END XSPI2_MspInit 0 */

  /** Initializes the peripherals clock
  */
    PeriphClkInit.PeriphClockSelection = RCC_PERIPHCLK_XSPI2;
    PeriphClkInit.Xspi2ClockSelection = RCC_XSPI2CLKSOURCE_PLL2S;
    if (HAL_RCCEx_PeriphCLKConfig(&PeriphClkInit) != HAL_OK)
    {
      Error_Handler();
    }

    /* Peripheral clock enable */
    __HAL_RCC_XSPIM_CLK_ENABLE();
    __HAL_RCC_XSPI2_CLK_ENABLE();

    __HAL_RCC_GPION_CLK_ENABLE();
    /**XSPI2 GPIO Configuration
    PN1     ------> XSPIM_P2_NCS1
    PN3     ------> XSPIM_P2_IO1
    PN0     ------> XSPIM_P2_DQS0
    PN11     ------> XSPIM_P2_IO7
    PN10     ------> XSPIM_P2_IO6
    PN9     ------> XSPIM_P2_IO5
    PN2     ------> XSPIM_P2_IO0
    PN6     ------> XSPIM_P2_CLK
    PN8     ------> XSPIM_P2_IO4
    PN4     ------> XSPIM_P2_IO2
    PN5     ------> XSPIM_P2_IO3
    */
    GPIO_InitStruct.Pin = GPIO_PIN_1|GPIO_PIN_3|GPIO_PIN_0|GPIO_PIN_11
                          |GPIO_PIN_10|GPIO_PIN_9|GPIO_PIN_2|GPIO_PIN_6
                          |GPIO_PIN_8|GPIO_PIN_4|GPIO_PIN_5;
    GPIO_InitStruct.Mode = GPIO_MODE_AF_PP;
    GPIO_InitStruct.Pull = GPIO_NOPULL;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_VERY_HIGH;
    GPIO_InitStruct.Alternate = GPIO_AF9_XSPIM_P2;
    HAL_GPIO_Init(GPION, &GPIO_InitStruct);

  /* USER CODE BEGIN XSPI2_MspInit 1 */

  /* USER CODE END XSPI2_MspInit 1 */
  }
#endif
}

/**
* @brief XSPI MSP De-Initialization
* This function freeze the hardware resources used in this example
* @param hxspi: XSPI handle pointer
* @retval None
*/
void HAL_XSPI_MspDeInit(XSPI_HandleTypeDef* hxspi)
{
#ifdef BOOTLOADER
  if(hxspi->Instance==XSPI2)
  {
  /* USER CODE BEGIN XSPI2_MspDeInit 0 */

  /* USER CODE END XSPI2_MspDeInit 0 */
    /* Peripheral clock disable */
    __HAL_RCC_XSPIM_CLK_DISABLE();
    __HAL_RCC_XSPI2_CLK_DISABLE();

    /**XSPI2 GPIO Configuration
    PN1     ------> XSPIM_P2_NCS1
    PN3     ------> XSPIM_P2_IO1
    PN0     ------> XSPIM_P2_DQS0
    PN11     ------> XSPIM_P2_IO7
    PN10     ------> XSPIM_P2_IO6
    PN9     ------> XSPIM_P2_IO5
    PN2     ------> XSPIM_P2_IO0
    PN6     ------> XSPIM_P2_CLK
    PN8     ------> XSPIM_P2_IO4
    PN4     ------> XSPIM_P2_IO2
    PN5     ------> XSPIM_P2_IO3
    */
    HAL_GPIO_DeInit(GPION, GPIO_PIN_1|GPIO_PIN_3|GPIO_PIN_0|GPIO_PIN_11
                          |GPIO_PIN_10|GPIO_PIN_9|GPIO_PIN_2|GPIO_PIN_6
                          |GPIO_PIN_8|GPIO_PIN_4|GPIO_PIN_5);

  /* USER CODE BEGIN XSPI2_MspDeInit 1 */

  /* USER CODE END XSPI2_MspDeInit 1 */
  }
#endif
}

/* USER CODE BEGIN 1 */

/**
 * @brief Ethernet MSP Init — Nucleo-H7S3L8 board.
 *
 * Configures PLL3 for 50 MHz ETH PHY clock, enables ETH peripheral clocks,
 * and initialises all RMII GPIO pins for the onboard LAN8742A PHY.
 */
void HAL_ETH_MspInit(ETH_HandleTypeDef* ethHandle)
{
    if (ethHandle->Instance == ETH)
    {
        GPIO_InitTypeDef GPIO_InitStruct = {0};
        RCC_PeriphCLKInitTypeDef PeriphClkInit = {0};

        /* Configure PLL3 to produce 50 MHz for ETH PHY reference clock */
        RCC_OscInitTypeDef RCC_OscInitStruct = {0};
        RCC_OscInitStruct.OscillatorType  = RCC_OSCILLATORTYPE_NONE;
        RCC_OscInitStruct.PLL3.PLLState   = RCC_PLL_ON;
        RCC_OscInitStruct.PLL3.PLLSource  = RCC_PLLSOURCE_HSI;
        RCC_OscInitStruct.PLL3.PLLM       = 4;
        RCC_OscInitStruct.PLL3.PLLN       = 25;
        RCC_OscInitStruct.PLL3.PLLP       = 2;
        RCC_OscInitStruct.PLL3.PLLQ       = 2;
        RCC_OscInitStruct.PLL3.PLLR       = 2;
        RCC_OscInitStruct.PLL3.PLLS       = 8;
        RCC_OscInitStruct.PLL3.PLLT       = 2;
        RCC_OscInitStruct.PLL3.PLLFractional = 0;
        HAL_RCC_OscConfig(&RCC_OscInitStruct);

        /* Route ETH ref clock from PHY and PHY clock from PLL3S */
        PeriphClkInit.PeriphClockSelection  = RCC_PERIPHCLK_ETH1REF | RCC_PERIPHCLK_ETH1PHY;
        PeriphClkInit.Eth1RefClockSelection = RCC_ETH1REFCLKSOURCE_PHY;
        PeriphClkInit.Eth1PhyClockSelection = RCC_ETH1PHYCLKSOURCE_PLL3S;
        HAL_RCCEx_PeriphCLKConfig(&PeriphClkInit);

        /* Enable ETH peripheral clocks */
        __HAL_RCC_ETH1MAC_CLK_ENABLE();
        __HAL_RCC_ETH1TX_CLK_ENABLE();
        __HAL_RCC_ETH1RX_CLK_ENABLE();

        /* Enable GPIO port clocks */
        __HAL_RCC_GPIOA_CLK_ENABLE();
        __HAL_RCC_GPIOB_CLK_ENABLE();
        __HAL_RCC_GPIOD_CLK_ENABLE();
        __HAL_RCC_GPIOG_CLK_ENABLE();
        __HAL_RCC_SBS_CLK_ENABLE();

        /* PD4 — ETH_PHY_INTN */
        GPIO_InitStruct.Pin       = GPIO_PIN_4;
        GPIO_InitStruct.Mode      = GPIO_MODE_AF_PP;
        GPIO_InitStruct.Pull      = GPIO_NOPULL;
        GPIO_InitStruct.Speed     = GPIO_SPEED_FREQ_HIGH;
        GPIO_InitStruct.Alternate = GPIO_AF11_ETH;
        HAL_GPIO_Init(GPIOD, &GPIO_InitStruct);

        /* PB6 — ETH_RMII_REF_CLK */
        GPIO_InitStruct.Pin = GPIO_PIN_6;
        HAL_GPIO_Init(GPIOB, &GPIO_InitStruct);

        /* PG4,5,6,11,12,13 — RXD0, RXD1, MDC, TX_EN, TXD1, TXD0 */
        GPIO_InitStruct.Pin = GPIO_PIN_4 | GPIO_PIN_5 | GPIO_PIN_6 |
                              GPIO_PIN_11 | GPIO_PIN_12 | GPIO_PIN_13;
        HAL_GPIO_Init(GPIOG, &GPIO_InitStruct);

        /* PA2 — ETH_MDIO, PA7 — ETH_RMII_CRS_DV */
        GPIO_InitStruct.Pin = GPIO_PIN_2 | GPIO_PIN_7;
        HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);

        /* ETH interrupt — priority 5, sub-priority 0 */
        HAL_NVIC_SetPriority(ETH_IRQn, 5, 0);
        HAL_NVIC_EnableIRQ(ETH_IRQn);
    }
}

/**
 * @brief Ethernet MSP DeInit — Nucleo-H7S3L8 board.
 */
void HAL_ETH_MspDeInit(ETH_HandleTypeDef* ethHandle)
{
    if (ethHandle->Instance == ETH)
    {
        __HAL_RCC_ETH1MAC_CLK_DISABLE();
        __HAL_RCC_ETH1TX_CLK_DISABLE();
        __HAL_RCC_ETH1RX_CLK_DISABLE();

        HAL_GPIO_DeInit(GPIOD, GPIO_PIN_4);
        HAL_GPIO_DeInit(GPIOB, GPIO_PIN_6);
        HAL_GPIO_DeInit(GPIOG, GPIO_PIN_4 | GPIO_PIN_5 | GPIO_PIN_6 |
                               GPIO_PIN_11 | GPIO_PIN_12 | GPIO_PIN_13);
        HAL_GPIO_DeInit(GPIOA, GPIO_PIN_2 | GPIO_PIN_7);

        HAL_NVIC_DisableIRQ(ETH_IRQn);
    }
}

/* USER CODE END 1 */

