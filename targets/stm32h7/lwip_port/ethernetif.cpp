#include "ethernetif.h"
#include "IEth.h"
#include "net_log.h"

#include "lwip/opt.h"
#include "lwip/def.h"
#include "lwip/mem.h"
#include "lwip/pbuf.h"
#include "lwip/stats.h"
#include "lwip/etharp.h"

// ── Internal state struct ──────────────────────────────────────────────────
struct EthernetIfState {
    IEth*       driver;
    const char* hostname;
};

// ── Global netif pointer (read by Stm32H7Eth::ProcessRx) ──────────────────
struct netif* g_netif_ptr = nullptr;

// ── Low-level output (lwIP → MAC) ─────────────────────────────────────────
static err_t low_level_output(struct netif* netif, struct pbuf* p) {
    auto* state = static_cast<EthernetIfState*>(netif->state);
    if (state && state->driver) {
        return state->driver->Transmit(p) ? ERR_OK : ERR_IF;
    }
    return ERR_IF;
}

// ── netif init callback ────────────────────────────────────────────────────
extern "C" err_t ethernetif_init(struct netif* netif) {
    LWIP_ASSERT("netif != NULL", netif != nullptr);

    auto* state = static_cast<EthernetIfState*>(netif->state);
    LWIP_ASSERT("netif->state != NULL", state != nullptr);
    LWIP_ASSERT("state->driver != NULL", state->driver != nullptr);

#if LWIP_NETIF_HOSTNAME
    netif->hostname = state->hostname;
#endif

    netif->name[0] = 'e';
    netif->name[1] = 't';

    netif->output     = etharp_output;
    netif->linkoutput = low_level_output;

    netif->hwaddr_len = ETH_HWADDR_LEN;
    state->driver->GetMacAddress(netif->hwaddr);

    netif->mtu   = 1500;
    netif->flags = NETIF_FLAG_BROADCAST | NETIF_FLAG_ETHARP | NETIF_FLAG_LINK_UP;

    // Expose this netif globally so ProcessRx can deliver packets
    g_netif_ptr = netif;

    return ERR_OK;
}

// ── Poll for received packets ──────────────────────────────────────────────
extern "C" void ethernetif_input(struct netif* netif) {
    auto* state = static_cast<EthernetIfState*>(netif->state);
    if (state && state->driver) {
        state->driver->ProcessRx();
    }
}
