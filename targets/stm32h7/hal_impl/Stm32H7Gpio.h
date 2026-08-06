#pragma once
#include "IGpio.h"
#include <cstdint>

#include "stm32h7rsxx.h"

namespace stm32 {
namespace h7 {

class Stm32H7Gpio : public hal::IGpio {
public:
    Stm32H7Gpio(GPIO_TypeDef* port, uint16_t pin);

    void set() override;
    void reset() override;
    void toggle() override;
    bool read() override;

private:
    GPIO_TypeDef* m_port;
    uint16_t m_pin;
};

} // namespace h7
} // namespace stm32
