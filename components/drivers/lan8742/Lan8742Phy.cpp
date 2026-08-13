#include "Lan8742Phy.h"
#include "net_log.h"

Lan8742Phy::Lan8742Phy(uint8_t phy_address)
    : mdio_(nullptr), phy_addr_(phy_address) {}

void Lan8742Phy::attachMdio(hal::IMdio& mdio) {
    mdio_ = &mdio;
}

extern "C" void HAL_Delay(uint32_t Delay);
extern "C" uint32_t HAL_GetTick(void);

bool Lan8742Phy::init() {
    if (!mdio_) return false;
    uint16_t reg = 0;

    // Issue a software reset by setting BMCR bit 15
    if (!mdio_->read(phy_addr_, REG_BMCR, reg)) {
        LOG_ERR("LAN8742: BMCR read failed\r\n");
        return false;
    }
    reg |= BMCR_RESET;
    mdio_->write(phy_addr_, REG_BMCR, reg);

    // Wait up to 500 ms for reset bit to self-clear
    uint32_t tick = HAL_GetTick();
    do {
        HAL_Delay(10);
        if (!mdio_->read(phy_addr_, REG_BMCR, reg)) {
            LOG_ERR("LAN8742: BMCR poll failed\r\n");
            return false;
        }
    } while ((reg & BMCR_RESET) && (HAL_GetTick() - tick < 500));

    if (reg & BMCR_RESET) {
        LOG_ERR("LAN8742: Reset timed out\r\n");
        return false;
    }

    LOG_INFO("LAN8742: Init OK (PHY addr %u)\r\n", phy_addr_);
    return true;
}

bool Lan8742Phy::isLinkUp() {
    if (!mdio_) return false;
    uint16_t bmsr = 0;
    if (!mdio_->read(phy_addr_, REG_BMSR, bmsr)) {
        return false;
    }
    // Both link-up AND auto-neg-complete must be set
    return (bmsr & BMSR_LINK_UP) && (bmsr & BMSR_AUTONEG_COMPLETE);
}

uint32_t Lan8742Phy::getId() {
    if (!mdio_) return 0xFFFFFFFF;
    uint16_t id1 = 0, id2 = 0;
    if (!mdio_->read(phy_addr_, REG_PHYIDR1, id1)) return 0xFFFFFFFF;
    if (!mdio_->read(phy_addr_, REG_PHYIDR2, id2)) return 0xFFFFFFFF;
    return (static_cast<uint32_t>(id1) << 16) | id2;
}

bool Lan8742Phy::getLinkConfig(hal::EthSpeed& speed, hal::EthDuplex& duplex) {
    if (!mdio_) return false;
    uint16_t physcsr = 0;
    if (!mdio_->read(phy_addr_, REG_PHYSCSR, physcsr)) {
        return false;
    }

    uint16_t mode = physcsr & PHYSCSR_SPEED_MASK;
    switch (mode) {
        case PHYSCSR_100FULL:
            speed  = hal::EthSpeed::Speed100M;
            duplex = hal::EthDuplex::Full;
            break;
        case PHYSCSR_100HALF:
            speed  = hal::EthSpeed::Speed100M;
            duplex = hal::EthDuplex::Half;
            break;
        case PHYSCSR_10FULL:
            speed  = hal::EthSpeed::Speed10M;
            duplex = hal::EthDuplex::Full;
            break;
        case PHYSCSR_10HALF:
        default:
            speed  = hal::EthSpeed::Speed10M;
            duplex = hal::EthDuplex::Half;
            break;
    }
    return true;
}
