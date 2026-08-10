#pragma once

#include "IPhy.h"

/**
 * @brief Concrete IPhy implementation for the Microchip LAN8742A.
 *
 * LAN8742A register map (standard MII, IEEE 802.3):
 *   Reg 0  BMCR  — Basic Mode Control
 *   Reg 1  BMSR  — Basic Mode Status
 *   Reg 2  PHYIDR1
 *   Reg 3  PHYIDR2
 *   Reg 31 PHYSCSR — PHY Special Control/Status (LAN8742A-specific)
 *
 * BMCR bit 15: Software Reset
 * BMSR bit  2: Link Status
 * BMSR bit  5: Auto-Negotiation Complete
 * PHYSCSR bits [4:2]: Speed/duplex indicator
 *   0b001 = 10BASE-T half-duplex
 *   0b010 = 100BASE-TX half-duplex
 *   0b101 = 10BASE-T full-duplex
 *   0b110 = 100BASE-TX full-duplex
 */
class Lan8742Phy : public IPhy {
public:
    /**
     * @param phy_address MDIO PHY address (0–31). On Nucleo-H7S3L8 this is 0.
     */
    explicit Lan8742Phy(uint8_t phy_address);

    bool Init(ETH_HandleTypeDef* heth) override;
    bool IsLinkUp(ETH_HandleTypeDef* heth) override;
    uint32_t GetId(ETH_HandleTypeDef* heth) override;
    bool GetLinkConfig(ETH_HandleTypeDef* heth,
                       uint32_t& speed_out,
                       uint32_t& duplex_out) override;

private:
    uint8_t phy_addr_;

    // LAN8742A register addresses
    static constexpr uint32_t REG_BMCR    = 0;
    static constexpr uint32_t REG_BMSR    = 1;
    static constexpr uint32_t REG_PHYIDR1 = 2;
    static constexpr uint32_t REG_PHYIDR2 = 3;
    static constexpr uint32_t REG_PHYSCSR = 31;

    // BMCR bits
    static constexpr uint32_t BMCR_RESET  = (1U << 15);

    // BMSR bits
    static constexpr uint32_t BMSR_LINK_UP   = (1U << 2);
    static constexpr uint32_t BMSR_AUTONEG_COMPLETE = (1U << 5);

    // PHYSCSR speed/duplex mask and values
    static constexpr uint32_t PHYSCSR_SPEED_MASK     = 0x001CU;  // bits [4:2]
    static constexpr uint32_t PHYSCSR_10HALF          = 0x0004U;  // 0b001 << 2
    static constexpr uint32_t PHYSCSR_100HALF         = 0x0008U;  // 0b010 << 2
    static constexpr uint32_t PHYSCSR_10FULL          = 0x0014U;  // 0b101 << 2
    static constexpr uint32_t PHYSCSR_100FULL         = 0x0018U;  // 0b110 << 2
};
