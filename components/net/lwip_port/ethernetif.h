#pragma once

#include "lwip/err.h"
#include "lwip/netif.h"
#include "IEth.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief lwIP netif init callback.
 *
 * netif->state must point to a valid EthernetIfState.
 */
err_t ethernetif_init(struct netif* netif);

/**
 * @brief Poll for received packets and forward them to lwIP.
 * Calls IEth::ProcessRx() on the driver stored in netif->state.
 */
void ethernetif_input(struct netif* netif);

#ifdef __cplusplus
}
#endif
