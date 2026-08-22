#pragma once
#include <cstdint>
#include <cstddef>

namespace hal {

class IUart {
public:
    virtual ~IUart() = default;

    virtual bool transmit(const uint8_t* data, size_t length) = 0;
    virtual bool receive(uint8_t* data, size_t length) = 0;
    virtual bool receive_byte(uint8_t& byte, uint32_t timeout_ms = 0) {
        (void)timeout_ms;
        return receive(&byte, 1);
    }
};

} // namespace hal
