#pragma once
#include <cstdint>
#include <cstddef>

namespace hal {

class ISpi {
public:
    virtual ~ISpi() = default;

    virtual bool transmitReceive(const uint8_t* txData, uint8_t* rxData, size_t length) = 0;
    virtual bool transmit(const uint8_t* data, size_t length) = 0;
    virtual bool receive(uint8_t* data, size_t length) = 0;
};

} // namespace hal
