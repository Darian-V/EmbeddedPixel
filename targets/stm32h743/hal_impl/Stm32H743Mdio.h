#pragma once
#include "IMdio.h"
#include "stm32h7xx_hal.h"

namespace stm32 {
namespace h743 {

class Stm32H743Mdio : public hal::IMdio {
public:
    explicit Stm32H743Mdio(ETH_HandleTypeDef* heth);
    bool read(uint8_t phy_addr, uint8_t reg_addr, uint16_t& value) override;
    bool write(uint8_t phy_addr, uint8_t reg_addr, uint16_t value) override;
private:
    ETH_HandleTypeDef* heth_;
};

} // namespace h743
} // namespace stm32
