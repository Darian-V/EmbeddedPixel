#pragma once
#include <cstdint>

namespace hal {

class IMdio {
public:
    virtual ~IMdio() = default;
    virtual bool read(uint8_t phy_addr, uint8_t reg_addr, uint16_t& value) = 0;
    virtual bool write(uint8_t phy_addr, uint8_t reg_addr, uint16_t value) = 0;
};

} // namespace hal
