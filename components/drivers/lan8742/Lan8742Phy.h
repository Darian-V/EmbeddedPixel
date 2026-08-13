#pragma once
#include "IPhy.h"
#include "IMdio.h"

class Lan8742Phy : public hal::IPhy {
public:
    explicit Lan8742Phy(uint8_t phy_address);

    void attachMdio(hal::IMdio& mdio) override;
    bool init() override;
    bool isLinkUp() override;
    uint32_t getId() override;
    bool getLinkConfig(hal::EthSpeed& speed, hal::EthDuplex& duplex) override;

private:
    hal::IMdio* mdio_;
    uint8_t phy_addr_;

    // LAN8742A register addresses
    static constexpr uint8_t REG_BMCR    = 0;
    static constexpr uint8_t REG_BMSR    = 1;
    static constexpr uint8_t REG_PHYIDR1 = 2;
    static constexpr uint8_t REG_PHYIDR2 = 3;
    static constexpr uint8_t REG_PHYSCSR = 31;

    // BMCR bits
    static constexpr uint16_t BMCR_RESET  = (1U << 15);

    // BMSR bits
    static constexpr uint16_t BMSR_LINK_UP         = (1U << 2);
    static constexpr uint16_t BMSR_AUTONEG_COMPLETE = (1U << 5);

    // PHYSCSR speed/duplex mask and values
    static constexpr uint16_t PHYSCSR_SPEED_MASK = 0x001CU;  // bits [4:2]
    static constexpr uint16_t PHYSCSR_10HALF     = 0x0004U;  // 0b001 << 2
    static constexpr uint16_t PHYSCSR_100HALF    = 0x0008U;  // 0b010 << 2
    static constexpr uint16_t PHYSCSR_10FULL     = 0x0014U;  // 0b101 << 2
    static constexpr uint16_t PHYSCSR_100FULL    = 0x0018U;  // 0b110 << 2
};
