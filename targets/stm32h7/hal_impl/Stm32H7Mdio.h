#pragma once
#include "IMdio.h"
#include "stm32h7rsxx_hal.h"

namespace stm32 {
namespace h7 {

class Stm32H7Mdio : public hal::IMdio {
public:
    explicit Stm32H7Mdio(ETH_HandleTypeDef* heth);
    bool read(uint8_t phy_addr, uint8_t reg_addr, uint16_t& value) override;
    bool write(uint8_t phy_addr, uint8_t reg_addr, uint16_t value) override;
private:
    ETH_HandleTypeDef* heth_;
};

} // namespace h7
} // namespace stm32
