#include "Stm32H7Eth.h"
#include <string.h>

#define ETH_RX_DESC_CNT 4
#define ETH_TX_DESC_CNT 4
#define ETH_MAX_PAYLOAD 1536

__attribute__((aligned(32), section(".eth_rx_buffers"))) ETH_DMADescTypeDef DMARxDscrTab[ETH_RX_DESC_CNT];
__attribute__((aligned(32), section(".eth_tx_buffers"))) ETH_DMADescTypeDef DMATxDscrTab[ETH_TX_DESC_CNT];
__attribute__((aligned(32), section(".eth_rx_buffers"))) uint8_t Rx_Buff[ETH_RX_DESC_CNT][ETH_MAX_PAYLOAD];
__attribute__((aligned(32), section(".eth_tx_buffers"))) uint8_t Tx_Buff[ETH_TX_DESC_CNT][ETH_MAX_PAYLOAD];

static ETH_HandleTypeDef* g_heth = nullptr;

extern "C" void ETH_IRQHandler(void) {
    if (g_heth) {
        HAL_ETH_IRQHandler(g_heth);
    }
}

extern "C" void HAL_ETH_MspInit(ETH_HandleTypeDef* ethHandle) {
    if(ethHandle->Instance == ETH) {
        GPIO_InitTypeDef GPIO_InitStruct = {0};
        RCC_PeriphCLKInitTypeDef PeriphClkInit = {0};

        /* Configure PLL3 for Ethernet PHY (50MHz) */
        RCC_OscInitTypeDef RCC_OscInitStruct = {0};
        RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_NONE;
        RCC_OscInitStruct.PLL3.PLLState = RCC_PLL_ON;
        RCC_OscInitStruct.PLL3.PLLSource = RCC_PLLSOURCE_HSI;
        RCC_OscInitStruct.PLL3.PLLM = 4;
        RCC_OscInitStruct.PLL3.PLLN = 25;
        RCC_OscInitStruct.PLL3.PLLP = 2;
        RCC_OscInitStruct.PLL3.PLLQ = 2;
        RCC_OscInitStruct.PLL3.PLLR = 2;
        RCC_OscInitStruct.PLL3.PLLS = 8;
        RCC_OscInitStruct.PLL3.PLLT = 2;
        RCC_OscInitStruct.PLL3.PLLFractional = 0;
        HAL_RCC_OscConfig(&RCC_OscInitStruct);

        /* Configure clocks */
        PeriphClkInit.PeriphClockSelection = RCC_PERIPHCLK_ETH1REF|RCC_PERIPHCLK_ETH1PHY;
        PeriphClkInit.Eth1RefClockSelection = RCC_ETH1REFCLKSOURCE_PHY;
        PeriphClkInit.Eth1PhyClockSelection = RCC_ETH1PHYCLKSOURCE_PLL3S;
        HAL_RCCEx_PeriphCLKConfig(&PeriphClkInit);

        /* Enable Peripheral clock */
        __HAL_RCC_ETH1MAC_CLK_ENABLE();
        __HAL_RCC_ETH1TX_CLK_ENABLE();
        __HAL_RCC_ETH1RX_CLK_ENABLE();

        __HAL_RCC_GPIOA_CLK_ENABLE();
        __HAL_RCC_GPIOB_CLK_ENABLE();
        __HAL_RCC_GPIOG_CLK_ENABLE();
        __HAL_RCC_GPIOD_CLK_ENABLE();
        
        __HAL_RCC_SBS_CLK_ENABLE();

        // PD4 -> ETH_PHY_INTN / AF11
        GPIO_InitStruct.Pin = GPIO_PIN_4;
        GPIO_InitStruct.Mode = GPIO_MODE_AF_PP;
        GPIO_InitStruct.Pull = GPIO_NOPULL;
        GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_HIGH;
        GPIO_InitStruct.Alternate = GPIO_AF11_ETH;
        HAL_GPIO_Init(GPIOD, &GPIO_InitStruct);

        // PB6 -> ETH_RMII_REF_CLK
        GPIO_InitStruct.Pin = GPIO_PIN_6;
        GPIO_InitStruct.Mode = GPIO_MODE_AF_PP;
        GPIO_InitStruct.Pull = GPIO_NOPULL;
        GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_HIGH;
        GPIO_InitStruct.Alternate = GPIO_AF11_ETH;
        HAL_GPIO_Init(GPIOB, &GPIO_InitStruct);

        // PG4 -> ETH_RMII_RXD0, PG5 -> ETH_RMII_RXD1, PG6 -> ETH_MDC
        // PG11 -> ETH_RMII_TX_EN, PG12 -> ETH_RMII_TXD1, PG13 -> ETH_RMII_TXD0
        GPIO_InitStruct.Pin = GPIO_PIN_4 | GPIO_PIN_5 | GPIO_PIN_6 | GPIO_PIN_11 | GPIO_PIN_12 | GPIO_PIN_13;
        GPIO_InitStruct.Mode = GPIO_MODE_AF_PP;
        GPIO_InitStruct.Pull = GPIO_NOPULL;
        GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_HIGH;
        GPIO_InitStruct.Alternate = GPIO_AF11_ETH;
        HAL_GPIO_Init(GPIOG, &GPIO_InitStruct);

        // PA2 -> ETH_MDIO, PA7 -> ETH_RMII_CRS_DV
        GPIO_InitStruct.Pin = GPIO_PIN_2 | GPIO_PIN_7;
        GPIO_InitStruct.Mode = GPIO_MODE_AF_PP;
        GPIO_InitStruct.Pull = GPIO_NOPULL;
        GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_HIGH;
        GPIO_InitStruct.Alternate = GPIO_AF11_ETH;
        HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);

        /* Peripheral interrupt init */
        HAL_NVIC_SetPriority(ETH_IRQn, 5, 0);
        HAL_NVIC_EnableIRQ(ETH_IRQn);
    }
}

extern "C" void HAL_ETH_MspDeInit(ETH_HandleTypeDef* ethHandle) {
    if(ethHandle->Instance==ETH) {
        __HAL_RCC_ETH1MAC_CLK_DISABLE();
        __HAL_RCC_ETH1TX_CLK_DISABLE();
        __HAL_RCC_ETH1RX_CLK_DISABLE();

        HAL_GPIO_DeInit(GPIOD, GPIO_PIN_4);
        HAL_GPIO_DeInit(GPIOB, GPIO_PIN_6);
        HAL_GPIO_DeInit(GPIOG, GPIO_PIN_4|GPIO_PIN_5|GPIO_PIN_6|GPIO_PIN_11|GPIO_PIN_12|GPIO_PIN_13);
        HAL_GPIO_DeInit(GPIOA, GPIO_PIN_2|GPIO_PIN_7);
        HAL_NVIC_DisableIRQ(ETH_IRQn);
    }
}

Stm32H7Eth::Stm32H7Eth() : link_is_up(false) {
    // No interrupts, polling mode only
    // HAL_NVIC_SetPriority(ETH_IRQn, 5, 0);
    // HAL_NVIC_EnableIRQ(ETH_IRQn);
}

Stm32H7Eth::~Stm32H7Eth() {
    HAL_ETH_DeInit(&heth);
}

bool Stm32H7Eth::Init() {
    // Ensure SRAM1 is clocked, otherwise all accesses to 0x30000000 are dropped!
    __HAL_RCC_SRAM1_CLK_ENABLE();

    // Configure MPU for SRAMAHB 0x30000000 to be Non-Cacheable for Ethernet DMA
    MPU_Region_InitTypeDef MPU_InitStruct = {0};
    HAL_MPU_Disable();

    MPU_InitStruct.Enable = MPU_REGION_ENABLE;
    MPU_InitStruct.Number = MPU_REGION_NUMBER7; 
    MPU_InitStruct.BaseAddress = 0x30000000;
    MPU_InitStruct.Size = MPU_REGION_SIZE_32KB;
    MPU_InitStruct.SubRegionDisable = 0x0;
    MPU_InitStruct.TypeExtField = MPU_TEX_LEVEL1; 
    MPU_InitStruct.AccessPermission = MPU_REGION_FULL_ACCESS;
    MPU_InitStruct.DisableExec = MPU_INSTRUCTION_ACCESS_DISABLE;
    MPU_InitStruct.IsShareable = MPU_ACCESS_SHAREABLE;
    MPU_InitStruct.IsCacheable = MPU_ACCESS_NOT_CACHEABLE;
    MPU_InitStruct.IsBufferable = MPU_ACCESS_NOT_BUFFERABLE;
    
    HAL_MPU_ConfigRegion(&MPU_InitStruct);
    HAL_MPU_Enable(MPU_PRIVILEGED_DEFAULT);

    // Invalidate cache to ensure no stale lines remain for the newly non-cacheable region
    SCB_CleanInvalidateDCache();
    __DSB();
    __ISB();

    uint8_t macAddr[6] = {0x00, 0x80, 0xE1, 0x11, 0x22, 0x33};

    heth.Instance = ETH;
    heth.Init.MACAddr = macAddr;
    heth.Init.MediaInterface = HAL_ETH_RMII_MODE;

    heth.Init.TxDesc = DMATxDscrTab;
    heth.Init.RxDesc = DMARxDscrTab;
    heth.Init.RxBuffLen = ETH_MAX_PAYLOAD;

    g_heth = &heth;
    
    // Force a complete clean and invalidate of the entire D-Cache just in case 
    // the startup code cached these regions before we configured the MPU.
    SCB_CleanInvalidateDCache();

    if (HAL_ETH_Init(&heth) != HAL_OK) {
        return false;
    }

    uint32_t phyreg = 0;
    if (HAL_ETH_ReadPHYRegister(&heth, 0, 0, &phyreg) == HAL_OK) {
        phyreg |= 0x8000;
        HAL_ETH_WritePHYRegister(&heth, 0, 0, phyreg);
        HAL_Delay(50); // Wait for PHY reset
    }

    ETH_MACConfigTypeDef MACConf;
    if (HAL_ETH_GetMACConfig(&heth, &MACConf) == HAL_OK) {
        MACConf.DuplexMode = ETH_FULLDUPLEX_MODE;
        MACConf.Speed = ETH_SPEED_100M;
        HAL_ETH_SetMACConfig(&heth, &MACConf);
    }

    ETH_MACFilterConfigTypeDef filterConf;
    if (HAL_ETH_GetMACFilterConfig(&heth, &filterConf) == HAL_OK) {
        filterConf.PromiscuousMode = ENABLE;
        HAL_ETH_SetMACFilterConfig(&heth, &filterConf);
    }

    // MAC start moved to WaitForLink() to ensure stable RMII clock
    return true;
}

#include "lwip/pbuf.h"
#include "lwip/netif.h"
#include "lwip/err.h"

static uint32_t rx_alloc_idx = 0;

extern "C" void HAL_ETH_RxAllocateCallback(uint8_t **buff) {
    *buff = Rx_Buff[rx_alloc_idx];
    rx_alloc_idx = (rx_alloc_idx + 1) % ETH_RX_DESC_CNT;
}

bool Stm32H7Eth::Transmit(struct pbuf *p) {
    if (p->tot_len > ETH_MAX_PAYLOAD) return false;

    pbuf_copy_partial(p, Tx_Buff[0], p->tot_len, 0);

    uint32_t alignedAddr = (uint32_t)Tx_Buff[0] & ~0x1FUL;
    uint32_t alignedSize = (((uint32_t)Tx_Buff[0] - alignedAddr) + p->tot_len + 0x1FUL) & ~0x1FUL;
    SCB_CleanDCache_by_Addr((uint32_t *)alignedAddr, alignedSize);

    ETH_BufferTypeDef Txbuffer;
    Txbuffer.buffer = Tx_Buff[0];
    Txbuffer.len = p->tot_len;
    Txbuffer.next = NULL;

    ETH_TxPacketConfig TxConfig;
    memset(&TxConfig, 0, sizeof(TxConfig));
    TxConfig.Attributes = ETH_TX_PACKETS_FEATURES_CRCPAD; 
    TxConfig.CRCPadCtrl = ETH_CRC_PAD_INSERT;
    TxConfig.Length = p->tot_len;
    TxConfig.TxBuffer = &Txbuffer;

    // Clean the Tx descriptors in cache so DMA sees the changes
    SCB_CleanDCache_by_Addr((uint32_t *)DMATxDscrTab, sizeof(DMATxDscrTab));

    printf("ETH: Tx %d bytes\r\n", p->tot_len);

    if (HAL_ETH_Transmit(&heth, &TxConfig, 100) != HAL_OK) {
        printf("ETH: Tx failed! Err=0x%lX, DMAErr=0x%lX, State=0x%lX\r\n", heth.ErrorCode, heth.DMAErrorCode, heth.gState);
        return false;
    }
    printf("ETH: Tx done\r\n");
    return true;
}

namespace net {
    extern struct netif gnetif;
}

struct RxPacketInfo {
    uint8_t* buff;
    uint16_t length;
};
extern "C" void HAL_ETH_RxLinkCallback(void **pStart, void **pEnd, uint8_t *buff, uint16_t Length) {
    // Invalidate the cache for the buffer the DMA just wrote to
    SCB_InvalidateDCache_by_Addr((uint32_t *)buff, Length);

    struct pbuf *p = pbuf_alloc(PBUF_RAW, Length, PBUF_POOL);
    if (p) {
        pbuf_take(p, buff, Length);
    }

    struct pbuf **ppStart = (struct pbuf **)pStart;
    struct pbuf **ppEnd = (struct pbuf **)pEnd;
    
    if (p == NULL) return; // Dropped packet (out of memory)

    if (!*ppStart) {
        *ppStart = p;
    } else {
        (*ppEnd)->next = p;
    }
    *ppEnd = p;
    
    for (struct pbuf *q = *ppStart; q != NULL; q = q->next) {
        q->tot_len += Length;
    }
}

void Stm32H7Eth::ProcessRx() {
    struct pbuf *p = NULL;
    
    // Invalidate Rx descriptors so CPU sees updates from DMA
    SCB_InvalidateDCache_by_Addr((uint32_t *)DMARxDscrTab, sizeof(DMARxDscrTab));

    while (HAL_ETH_ReadData(&heth, (void **)&p) == HAL_OK) {
        if (p != NULL) {
            printf("ETH: Rx %d bytes\r\n", p->tot_len);
            if (net::gnetif.input(p, &net::gnetif) != ERR_OK) {
                pbuf_free(p);
            }
        }
        // Descriptors are automatically updated by HAL_ETH_ReadData, clean them for DMA
        SCB_CleanDCache_by_Addr((uint32_t *)DMARxDscrTab, sizeof(DMARxDscrTab));
    }
}



extern "C" void HAL_ETH_TxFreeCallback(uint32_t *buff) {
    (void)buff;
}

bool Stm32H7Eth::IsLinkUp() {
    uint32_t phyreg1 = 0;
    if (HAL_ETH_ReadPHYRegister(&heth, 0, 1, &phyreg1) == HAL_OK) {
        return ((phyreg1 & 0x0004) != 0);
    }
    return false;
}

uint32_t Stm32H7Eth::GetPhyId() {
    uint32_t phyreg2 = 0;
    uint32_t phyreg3 = 0;
    // LAN8742 PHY address on Nucleo-H7S3L8 is usually 0
    if (HAL_ETH_ReadPHYRegister(&heth, 0, 2, &phyreg2) != HAL_OK) return 0xFFFFFFFF;
    if (HAL_ETH_ReadPHYRegister(&heth, 0, 3, &phyreg3) != HAL_OK) return 0xFFFFFFFF;
    return (phyreg2 << 16) | phyreg3;
}

bool Stm32H7Eth::WaitForLink(uint32_t timeout_ms) {
    uint32_t tickstart = HAL_GetTick();
    uint32_t phyreg1 = 0;
    while ((HAL_GetTick() - tickstart) < timeout_ms) {
        if (HAL_ETH_ReadPHYRegister(&heth, 0, 1, &phyreg1) == HAL_OK) {
            // Wait for both Link Up (bit 2) AND Auto-Negotiation Complete (bit 5)
            if ((phyreg1 & 0x0004) != 0 && (phyreg1 & 0x0020) != 0) { 
                printf("\r\n--- PHY REGISTERS DUMP ---\r\n");
                for (int i = 0; i <= 31; i++) {
                    uint32_t val = 0;
                    if (HAL_ETH_ReadPHYRegister(&heth, 0, i, &val) == HAL_OK) {
                        printf("Reg %02d: 0x%04lX\r\n", i, val);
                    }
                }
                printf("--------------------------\r\n");
                
                // Start MAC now that the PHY clock is fully stable
                HAL_ETH_Start(&heth);
                
                return true;
            }
        }
        HAL_Delay(10);
    }
    return false;
}

void Stm32H7Eth::GetMacAddress(uint8_t* mac_addr) {
    mac_addr[0] = 0x00;
    mac_addr[1] = 0x80;
    mac_addr[2] = 0xE1;
    mac_addr[3] = 0x11;
    mac_addr[4] = 0x22;
    mac_addr[5] = 0x33;
}

void Stm32H7Eth::Error_Handler() {
    while (1) { }
}

void Stm32H7Eth::PrintMmcCounters() {
    printf("--- MAC MMC Counters ---\r\n");
    printf("Desc0.DESC3: 0x%08lX\r\n", DMARxDscrTab[0].DESC3);
    printf("Desc Addr: %p, Buff Addr: %p\r\n", (void*)DMARxDscrTab, (void*)Rx_Buff);
    printf("MACCR: 0x%08lX\r\n", heth.Instance->MACCR);
    printf("DMACSR: 0x%08lX\r\n", heth.Instance->DMACSR);
    printf("MMCCR: 0x%08lX\r\n", heth.Instance->MMCCR);
    printf("Rx CRC Error: %lu\r\n", heth.Instance->MMCRCRCEPR);
    printf("Rx Alignment Error: %lu\r\n", heth.Instance->MMCRAEPR);
    printf("Rx Good Unicast: %lu\r\n", heth.Instance->MMCRUPGR);
    printf("Rx Packets total (Good/Bad): %lu\r\n", heth.Instance->MMCRLPITCR);
    printf("------------------------\r\n");
}
