#include "Stm32H7Eth.h"
#include "net_log.h"
#include <string.h>

#include "lwip/pbuf.h"
#include "lwip/netif.h"
#include "lwip/err.h"

// ── DMA buffer configuration ──────────────────────────────────────────────
#ifndef ETH_RX_DESC_CNT
#define ETH_RX_DESC_CNT  4
#endif
#ifndef ETH_TX_DESC_CNT
#define ETH_TX_DESC_CNT  4
#endif
#ifndef ETH_MAX_PAYLOAD
#define ETH_MAX_PAYLOAD  1536
#endif

__attribute__((aligned(32), section(".eth_rx_buffers")))
    ETH_DMADescTypeDef DMARxDscrTab[ETH_RX_DESC_CNT];

__attribute__((aligned(32), section(".eth_tx_buffers")))
    ETH_DMADescTypeDef DMATxDscrTab[ETH_TX_DESC_CNT];

__attribute__((aligned(32), section(".eth_rx_buffers")))
    uint8_t Rx_Buff[ETH_RX_DESC_CNT][ETH_MAX_PAYLOAD];

__attribute__((aligned(32), section(".eth_tx_buffers")))
    uint8_t Tx_Buff[ETH_TX_DESC_CNT][ETH_MAX_PAYLOAD];

// ── IRQ forwarding ─────────────────────────────────────────────────────────
static ETH_HandleTypeDef* g_heth = nullptr;

extern "C" void ETH_IRQHandler(void) {
    if (g_heth) {
        HAL_ETH_IRQHandler(g_heth);
        // Clear all DMA status flags to prevent hardware IRQ storms
        g_heth->Instance->DMACSR = g_heth->Instance->DMACSR;
    }
}

extern "C" void HAL_ETH_TxCpltCallback(ETH_HandleTypeDef *heth) {
    (void)heth;
}

extern "C" void HAL_ETH_RxCpltCallback(ETH_HandleTypeDef *heth) {
    (void)heth;
}

extern "C" void HAL_ETH_ErrorCallback(ETH_HandleTypeDef *heth) {
    (void)heth;
}

// ── RX allocation callback (called from HAL inside ProcessRx) ──────────────
static uint32_t rx_alloc_idx = 0;

extern "C" void HAL_ETH_RxAllocateCallback(uint8_t** buff) {
    *buff = Rx_Buff[rx_alloc_idx];
    rx_alloc_idx = (rx_alloc_idx + 1) % ETH_RX_DESC_CNT;
}

// ── RX link callback (called from HAL inside ProcessRx) ────────────────────
extern "C" void HAL_ETH_RxLinkCallback(void** pStart, void** pEnd,
                                        uint8_t* buff, uint16_t Length) {
    SCB_InvalidateDCache_by_Addr((uint32_t*)buff, Length);

    struct pbuf* p = pbuf_alloc(PBUF_RAW, Length, PBUF_POOL);
    if (p) pbuf_take(p, buff, Length);

    struct pbuf** ppStart = (struct pbuf**)pStart;
    struct pbuf** ppEnd   = (struct pbuf**)pEnd;

    if (!p) return; // out of memory, drop packet

    if (!*ppStart) {
        *ppStart = p;
    } else {
        (*ppEnd)->next = p;
    }
    *ppEnd = p;

    for (struct pbuf* q = *ppStart; q != nullptr; q = q->next) {
        q->tot_len += Length;
    }
}

// ── TX free callback ────────────────────────────────────────────────────────
extern "C" void HAL_ETH_TxFreeCallback(uint32_t* buff) {
    (void)buff;
}

// ── Constructor / Destructor ────────────────────────────────────────────────
Stm32H7Eth::Stm32H7Eth(const Stm32H7EthConfig& cfg, hal::IPhy& phy)
    : cfg_(cfg), phy_(phy), mdio_(&heth_) {}

Stm32H7Eth::~Stm32H7Eth() {
    HAL_ETH_DeInit(&heth_);
}

// ── Init ────────────────────────────────────────────────────────────────────
bool Stm32H7Eth::Init() {
    // Clock SRAMAHB so DMA buffers at 0x30000000 are accessible
    __HAL_RCC_SRAM1_CLK_ENABLE();

    // Configure MPU: SRAMAHB region non-cacheable for Ethernet DMA
    SCB_CleanInvalidateDCache();
    __DSB();
    __ISB();

    // Fill HAL handle
    memset(&heth_, 0, sizeof(heth_));
    heth_.Instance          = ETH;
    heth_.Init.MACAddr      = cfg_.mac_addr;
    heth_.Init.MediaInterface = static_cast<ETH_MediaInterfaceTypeDef>(cfg_.media_interface);
    heth_.Init.TxDesc       = DMATxDscrTab;
    heth_.Init.RxDesc       = DMARxDscrTab;
    heth_.Init.RxBuffLen    = ETH_MAX_PAYLOAD;

    g_heth = &heth_;

    SCB_CleanInvalidateDCache(); // flush before DMA ownership

    if (HAL_ETH_Init(&heth_) != HAL_OK) {
        LOG_ERR("HAL_ETH_Init failed\r\n");
        return false;
    }

    // Disable ETH NVIC IRQ since we process RX and TX via polling in FreeRTOS tasks
    HAL_NVIC_DisableIRQ(ETH_IRQn);

    // Delegate PHY soft-reset to IPhy
    phy_.attachMdio(mdio_);
    if (!phy_.init()) {
        LOG_ERR("PHY Init failed\r\n");
        return false;
    }

    return true;
}

// ── WaitForLink ─────────────────────────────────────────────────────────────
bool Stm32H7Eth::WaitForLink(uint32_t timeout_ms) {
    uint32_t tickstart = HAL_GetTick();

    while ((HAL_GetTick() - tickstart) < timeout_ms) {
        if (phy_.isLinkUp()) {
            // Read negotiated speed / duplex from PHY
            hal::EthSpeed halSpeed;
            hal::EthDuplex halDuplex;
            phy_.getLinkConfig(halSpeed, halDuplex);

            uint32_t speed = (halSpeed == hal::EthSpeed::Speed100M) ? ETH_SPEED_100M : ETH_SPEED_10M;
            uint32_t duplex = (halDuplex == hal::EthDuplex::Full) ? ETH_FULLDUPLEX_MODE : ETH_HALFDUPLEX_MODE;

            ETH_MACConfigTypeDef macConf;
            if (HAL_ETH_GetMACConfig(&heth_, &macConf) == HAL_OK) {
                macConf.Speed      = speed;
                macConf.DuplexMode = duplex;
                HAL_ETH_SetMACConfig(&heth_, &macConf);
            }

            // Dump PHY registers only at DEBUG level
            LOG_DBG("--- PHY REGISTERS ---\r\n");
            for (int i = 0; i <= 31; i++) {
                uint32_t val = 0;
                if (HAL_ETH_ReadPHYRegister(&heth_, 0, i, &val) == HAL_OK) {
                    LOG_DBG("Reg %02d: 0x%04lX\r\n", i, val);
                }
            }
            LOG_DBG("---------------------\r\n");

            HAL_ETH_Start(&heth_);

            // Enable promiscuous mode after MAC is started
            ETH_MACFilterConfigTypeDef filterConf;
            if (HAL_ETH_GetMACFilterConfig(&heth_, &filterConf) == HAL_OK) {
                filterConf.PromiscuousMode = ENABLE;
                HAL_ETH_SetMACFilterConfig(&heth_, &filterConf);
            }

            return true;
        }
        HAL_Delay(10);
    }
    return false;
}

// ── IsLinkUp ─────────────────────────────────────────────────────────────────
bool Stm32H7Eth::IsLinkUp() {
    return phy_.isLinkUp();
}

// ── GetPhyId ─────────────────────────────────────────────────────────────────
uint32_t Stm32H7Eth::GetPhyId() {
    return phy_.getId();
}

void Stm32H7Eth::SetRxCallback(RxCallback cb, void* user_data) {
    rx_cb_ = cb;
    rx_user_data_ = user_data;
}

// ── Transmit ─────────────────────────────────────────────────────────────────
static uint32_t tx_idx = 0;

bool Stm32H7Eth::Transmit(const uint8_t* buffer, uint16_t length) {
    if (length > ETH_MAX_PAYLOAD || buffer == nullptr) return false;

    uint8_t* buf = Tx_Buff[tx_idx];
    tx_idx = (tx_idx + 1) % ETH_TX_DESC_CNT;

    memcpy(buf, buffer, length);

    uint32_t alignedAddr = (uint32_t)buf & ~0x1FUL;
    uint32_t alignedSize = (((uint32_t)buf - alignedAddr) + length + 0x1FUL) & ~0x1FUL;
    SCB_CleanDCache_by_Addr((uint32_t*)alignedAddr, alignedSize);

    ETH_BufferTypeDef txBuf;
    txBuf.buffer = buf;
    txBuf.len    = length;
    txBuf.next   = nullptr;

    ETH_TxPacketConfig txCfg;
    memset(&txCfg, 0, sizeof(txCfg));
    txCfg.Attributes  = ETH_TX_PACKETS_FEATURES_CRCPAD;
    txCfg.CRCPadCtrl  = ETH_CRC_PAD_INSERT;
    txCfg.Length      = length;
    txCfg.TxBuffer    = &txBuf;

    SCB_CleanDCache_by_Addr((uint32_t*)DMATxDscrTab, sizeof(DMATxDscrTab));

    if (HAL_ETH_Transmit(&heth_, &txCfg, 100) != HAL_OK) {
        LOG_ERR("ETH Tx failed Err=0x%lX DMAErr=0x%lX State=0x%lX\r\n",
                heth_.ErrorCode, heth_.DMAErrorCode, heth_.gState);
        return false;
    }
    return true;
}

// ── ProcessRx ────────────────────────────────────────────────────────────────
void Stm32H7Eth::ProcessRx() {
    struct pbuf* p = nullptr;

    SCB_InvalidateDCache_by_Addr((uint32_t*)DMARxDscrTab, sizeof(DMARxDscrTab));

    while (HAL_ETH_ReadData(&heth_, (void**)&p) == HAL_OK) {
        if (p != nullptr) {
            if (rx_cb_) {
                rx_cb_(rx_user_data_, p);
            } else {
                pbuf_free(p);
            }
        }
        SCB_InvalidateDCache_by_Addr((uint32_t*)DMARxDscrTab, sizeof(DMARxDscrTab));
    }
}

// ── GetMacAddress ─────────────────────────────────────────────────────────────
void Stm32H7Eth::GetMacAddress(uint8_t* mac_addr) {
    for (int i = 0; i < 6; i++) mac_addr[i] = cfg_.mac_addr[i];
}

// ── PrintMmcCounters ──────────────────────────────────────────────────────────
void Stm32H7Eth::PrintMmcCounters() {
    LOG_INFO("--- MAC MMC Counters ---\r\n");
    LOG_INFO("Desc0.DESC3: 0x%08lX\r\n",   DMARxDscrTab[0].DESC3);
    LOG_INFO("Desc Addr: %p  Buff Addr: %p\r\n", (void*)DMARxDscrTab, (void*)Rx_Buff);
    LOG_INFO("MACCR:  0x%08lX\r\n",         heth_.Instance->MACCR);
    LOG_INFO("DMACSR: 0x%08lX\r\n",         heth_.Instance->DMACSR);
    LOG_INFO("MMCCR:  0x%08lX\r\n",         heth_.Instance->MMCCR);
    LOG_INFO("Rx CRC Error:       %lu\r\n", heth_.Instance->MMCRCRCEPR);
    LOG_INFO("Rx Alignment Error: %lu\r\n", heth_.Instance->MMCRAEPR);
    LOG_INFO("Rx Good Unicast:    %lu\r\n", heth_.Instance->MMCRUPGR);
    LOG_INFO("Rx Packets total:   %lu\r\n", heth_.Instance->MMCRLPITCR);
    LOG_INFO("------------------------\r\n");
}
