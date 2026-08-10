#include "Lan8742Phy.h"
#include "net_log.h"
#include "stm32h7rsxx_hal.h"

Lan8742Phy::Lan8742Phy(uint8_t phy_address) : phy_addr_(phy_address) {}

bool Lan8742Phy::Init(ETH_HandleTypeDef* heth) {
    uint32_t reg = 0;

    // Issue a software reset by setting BMCR bit 15
    if (HAL_ETH_ReadPHYRegister(heth, phy_addr_, REG_BMCR, &reg) != HAL_OK) {
        LOG_ERR("LAN8742: BMCR read failed\r\n");
        return false;
    }
    reg |= BMCR_RESET;
    HAL_ETH_WritePHYRegister(heth, phy_addr_, REG_BMCR, reg);

    // Wait up to 500 ms for reset bit to self-clear
    uint32_t tick = HAL_GetTick();
    do {
        HAL_Delay(10);
        if (HAL_ETH_ReadPHYRegister(heth, phy_addr_, REG_BMCR, &reg) != HAL_OK) {
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

bool Lan8742Phy::IsLinkUp(ETH_HandleTypeDef* heth) {
    uint32_t bmsr = 0;
    if (HAL_ETH_ReadPHYRegister(heth, phy_addr_, REG_BMSR, &bmsr) != HAL_OK) {
        return false;
    }
    // Both link-up AND auto-neg-complete must be set
    return (bmsr & BMSR_LINK_UP) && (bmsr & BMSR_AUTONEG_COMPLETE);
}

uint32_t Lan8742Phy::GetId(ETH_HandleTypeDef* heth) {
    uint32_t id1 = 0, id2 = 0;
    if (HAL_ETH_ReadPHYRegister(heth, phy_addr_, REG_PHYIDR1, &id1) != HAL_OK) return 0xFFFFFFFF;
    if (HAL_ETH_ReadPHYRegister(heth, phy_addr_, REG_PHYIDR2, &id2) != HAL_OK) return 0xFFFFFFFF;
    return (id1 << 16) | id2;
}

bool Lan8742Phy::GetLinkConfig(ETH_HandleTypeDef* heth,
                               uint32_t& speed_out,
                               uint32_t& duplex_out) {
    uint32_t physcsr = 0;
    if (HAL_ETH_ReadPHYRegister(heth, phy_addr_, REG_PHYSCSR, &physcsr) != HAL_OK) {
        return false;
    }

    uint32_t mode = physcsr & PHYSCSR_SPEED_MASK;
    switch (mode) {
        case PHYSCSR_100FULL:
            speed_out  = ETH_SPEED_100M;
            duplex_out = ETH_FULLDUPLEX_MODE;
            break;
        case PHYSCSR_100HALF:
            speed_out  = ETH_SPEED_100M;
            duplex_out = ETH_HALFDUPLEX_MODE;
            break;
        case PHYSCSR_10FULL:
            speed_out  = ETH_SPEED_10M;
            duplex_out = ETH_FULLDUPLEX_MODE;
            break;
        case PHYSCSR_10HALF:
        default:
            speed_out  = ETH_SPEED_10M;
            duplex_out = ETH_HALFDUPLEX_MODE;
            break;
    }
    return true;
}
