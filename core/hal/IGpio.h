#pragma once
#include <cstdint>

namespace hal {

class IGpio {
public:
    virtual ~IGpio() = default;
    
    virtual void set() = 0;
    virtual void reset() = 0;
    virtual void toggle() = 0;
    virtual bool read() = 0;
};

} // namespace hal
