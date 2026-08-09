#ifndef STM32H7ETH_H
#define STM32H7ETH_H

#include "IEth.h"
#include "stm32h7rsxx_hal.h"

class Stm32H7Eth : public IEth {
public:
    Stm32H7Eth();
    virtual ~Stm32H7Eth();

    bool Init() override;
    bool IsLinkUp() override;
    void ProcessRx() override;
    void GetMacAddress(uint8_t* mac_addr) override;
    bool Transmit(struct pbuf *p) override;
    uint32_t GetPhyId() override;
    bool WaitForLink(uint32_t timeout_ms) override;

private:
    ETH_HandleTypeDef heth;
    bool link_is_up;

    void Error_Handler();
};

#endif // STM32H7ETH_H
