#pragma once
#include <cstdint>
#include <cstddef>

namespace hal {

class IUart {
public:
    virtual ~IUart() = default;

    virtual bool transmit(const uint8_t* data, size_t length) = 0;
    virtual bool receive(uint8_t* data, size_t length) = 0;
};

} // namespace hal
