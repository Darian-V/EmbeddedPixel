#pragma once
#include "IGpio.h"
#include <cstdint>

#include "stm32h7xx.h"

namespace stm32 {
namespace h743 {

class Stm32H743Gpio : public hal::IGpio {
public:
    Stm32H743Gpio(GPIO_TypeDef* port, uint16_t pin);

    void set() override;
    void reset() override;
    void toggle() override;
    bool read() override;

private:
    GPIO_TypeDef* m_port;
    uint16_t m_pin;
};

} // namespace h743
} // namespace stm32
