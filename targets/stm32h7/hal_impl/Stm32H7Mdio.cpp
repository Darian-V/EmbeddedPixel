#include "Stm32H7Mdio.h"

namespace stm32 {
namespace h7 {

Stm32H7Mdio::Stm32H7Mdio(ETH_HandleTypeDef* heth) : heth_(heth) {}

bool Stm32H7Mdio::read(uint8_t phy_addr, uint8_t reg_addr, uint16_t& value) {
    uint32_t reg32 = 0;
    if (HAL_ETH_ReadPHYRegister(heth_, phy_addr, reg_addr, &reg32) != HAL_OK) {
        return false;
    }
    value = static_cast<uint16_t>(reg32);
    return true;
}

bool Stm32H7Mdio::write(uint8_t phy_addr, uint8_t reg_addr, uint16_t value) {
    return HAL_ETH_WritePHYRegister(heth_, phy_addr, reg_addr, static_cast<uint32_t>(value)) == HAL_OK;
}

} // namespace h7
} // namespace stm32
