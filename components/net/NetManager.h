#ifndef NETMANAGER_H
#define NETMANAGER_H

#include "FreeRtosThread.h"
#include "IEth.h"

namespace net {

class NetManager : public osal::Runnable {
public:
    NetManager(IEth& ethDriver);
    ~NetManager() = default;

    void run() override;

private:
    IEth& eth;
};

} // namespace net

#endif // NETMANAGER_H
