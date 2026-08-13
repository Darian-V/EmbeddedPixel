#pragma once

#include "IEth.h"
#include "IPhy.h"
#include "Stm32H7Mdio.h"
#include "stm32h7rsxx_hal.h"

/**
 * @brief Configuration struct for Stm32H7Eth.
 *
 * All fields that were previously hardcoded in Init() / GetMacAddress().
 * Passed by value at construction time.
 */
struct Stm32H7EthConfig {
    uint8_t  mac_addr[6];       ///< 6-byte MAC address, e.g. {0x00,0x80,0xE1,0x11,0x22,0x33}
    uint32_t media_interface;   ///< HAL_ETH_RMII_MODE or HAL_ETH_MII_MODE
};

/**
 * @brief STM32H7 Ethernet MAC driver.
 *
 * Owns the HAL ETH handle and DMA descriptor/buffer arrays.
 * PHY operations are delegated to the injected IPhy reference.
 *
 * DMA buffer sizes are compile-time constants defined in the .cpp:
 *   ETH_RX_DESC_CNT 4
 *   ETH_TX_DESC_CNT 4
 *   ETH_MAX_PAYLOAD 1536
 */
class Stm32H7Eth : public IEth {
public:
    /**
     * @param cfg    MAC-level configuration (MAC address, media interface).
     * @param phy    Reference to an IPhy implementation. Must outlive this object.
     */
    Stm32H7Eth(const Stm32H7EthConfig& cfg, hal::IPhy& phy);
    ~Stm32H7Eth() override;

    bool Init() override;
    bool WaitForLink(uint32_t timeout_ms) override;
    bool IsLinkUp() override;
    void ProcessRx() override;
    bool Transmit(struct pbuf* p) override;
    void GetMacAddress(uint8_t* mac_addr) override;
    uint32_t GetPhyId() override;
    void PrintMmcCounters() override;

private:
    Stm32H7EthConfig cfg_;
    hal::IPhy& phy_;
    ETH_HandleTypeDef heth_;
    stm32::h7::Stm32H7Mdio mdio_;
};
