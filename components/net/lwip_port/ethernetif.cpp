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

// ── Low-level output (lwIP → MAC) ─────────────────────────────────────────
static err_t low_level_output(struct netif* netif, struct pbuf* p) {
    auto* state = static_cast<EthernetIfState*>(netif->state);
    if (!state || !state->driver) {
        return ERR_IF;
    }

    if (p->next == nullptr) {
        // Single contiguous pbuf
        return state->driver->Transmit(static_cast<const uint8_t*>(p->payload), p->len) ? ERR_OK : ERR_IF;
    } else {
        // Chained pbuf
        uint8_t buffer[1536];
        if (p->tot_len > sizeof(buffer)) {
            return ERR_MEM;
        }
        pbuf_copy_partial(p, buffer, p->tot_len, 0);
        return state->driver->Transmit(buffer, p->tot_len) ? ERR_OK : ERR_IF;
    }
}

// ── Packet reception callback from IEth driver ────────────────────────────
static void rx_packet_handler(void* user_data, void* packet) {
    auto* netif = static_cast<struct netif*>(user_data);
    auto* p = static_cast<struct pbuf*>(packet);
    if (netif && p) {
        if (netif->input(p, netif) != ERR_OK) {
            pbuf_free(p);
        }
    }
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

    // Register this netif as the packet receiver on the driver instance
    state->driver->SetRxCallback(rx_packet_handler, netif);

    return ERR_OK;
}

// ── Poll for received packets ──────────────────────────────────────────────
extern "C" void ethernetif_input(struct netif* netif) {
    auto* state = static_cast<EthernetIfState*>(netif->state);
    if (state && state->driver) {
        state->driver->ProcessRx();
    }
}
