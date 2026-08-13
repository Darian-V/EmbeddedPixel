#pragma once
#include <cstdint>
#include "IMdio.h"

namespace hal {

enum class EthSpeed { Speed10M, Speed100M, Speed1G };
enum class EthDuplex { Half, Full };

class IPhy {
public:
    virtual ~IPhy() = default;
    virtual void attachMdio(hal::IMdio& mdio) = 0;
    virtual bool init() = 0;
    virtual bool isLinkUp() = 0;
    virtual uint32_t getId() = 0;
    virtual bool getLinkConfig(EthSpeed& speed, EthDuplex& duplex) = 0;
};

} // namespace hal
