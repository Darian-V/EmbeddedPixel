#pragma once
#include <cstdint>
#include <cstddef>

namespace hal {

class II2c {
public:
    virtual ~II2c() = default;

    virtual bool write(uint16_t devAddress, const uint8_t* data, size_t length) = 0;
    virtual bool read(uint16_t devAddress, uint8_t* data, size_t length) = 0;
    
    virtual bool writeMem(uint16_t devAddress, uint16_t memAddress, uint16_t memAddSize, const uint8_t* data, size_t length) = 0;
    virtual bool readMem(uint16_t devAddress, uint16_t memAddress, uint16_t memAddSize, uint8_t* data, size_t length) = 0;
};

} // namespace hal
