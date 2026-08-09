#include "ethernetif.h"
#include "lwip/opt.h"
#include "lwip/def.h"
#include "lwip/mem.h"
#include "lwip/pbuf.h"
#include "lwip/stats.h"
#include "lwip/etharp.h"
#include "IEth.h"

// Pointer to the hardware Ethernet driver, set by NetManager before netif_add
extern IEth* g_eth_driver;
IEth* g_eth_driver = nullptr;

extern "C" {

static err_t low_level_output(struct netif *netif, struct pbuf *p) {
    if (g_eth_driver) {
        if (g_eth_driver->Transmit(p)) {
            return ERR_OK;
        }
    }
    return ERR_IF;
}

err_t ethernetif_init(struct netif *netif) {
    LWIP_ASSERT("netif != NULL", (netif != NULL));

#if LWIP_NETIF_HOSTNAME
    netif->hostname = "stm32h7rs";
#endif

    netif->name[0] = 's';
    netif->name[1] = 't';

    netif->output = etharp_output;
    netif->linkoutput = low_level_output;

    netif->hwaddr_len = ETH_HWADDR_LEN;
    if (g_eth_driver) {
        g_eth_driver->GetMacAddress(netif->hwaddr);
    }
    
    netif->mtu = 1500;
    netif->flags = NETIF_FLAG_BROADCAST | NETIF_FLAG_ETHARP | NETIF_FLAG_LINK_UP;

    return ERR_OK;
}

void ethernetif_input(struct netif *netif) {
    if (g_eth_driver) {
        g_eth_driver->ProcessRx();
    }
}

}
