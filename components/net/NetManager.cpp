#include "NetManager.h"
#include "net_log.h"
#include "ethernetif.h"

// lwIP
#include "lwip/init.h"
#include "lwip/netif.h"
#include "lwip/tcpip.h"
#include "lwip/dhcp.h"
#include "lwip/ip_addr.h"
#include "lwip/etharp.h"

// FreeRTOS
#include "FreeRTOS.h"
#include "task.h"

namespace net {

// ── Callbacks for thread-safe lwIP execution inside tcpip_thread ──────────

static void cb_fallback_static(void* arg) {
    LOG_DBG("NetManager: [tcpip_thread] entering cb_fallback_static\r\n");
    auto* self = static_cast<NetManager*>(arg);
    self->applyStaticIpInternal();
}

static void cb_link_down(void* arg) {
    LOG_DBG("NetManager: [tcpip_thread] executing cb_link_down\r\n");
    auto* self = static_cast<NetManager*>(arg);
    netif_set_link_down(&self->get_netif());
    if (self->config().mode != IpMode::STATIC) {
        dhcp_stop(&self->get_netif());
    }
}

static void cb_link_up(void* arg) {
    LOG_DBG("NetManager: [tcpip_thread] executing cb_link_up\r\n");
    auto* self = static_cast<NetManager*>(arg);
    netif_set_link_up(&self->get_netif());
    if (self->config().mode == IpMode::STATIC) {
        self->applyStaticIpInternal();
    } else {
        dhcp_start(&self->get_netif());
    }
}

static void on_tcpip_init_done(void* arg) {
    LOG_DBG("NetManager: [tcpip_thread] on_tcpip_init_done callback fired\r\n");
    auto* self = static_cast<NetManager*>(arg);
    self->initLwip();
}

// ── Constructor ────────────────────────────────────────────────────────────
NetManager::NetManager(IEth& ethDriver, const IpConfig& config)
    : eth_(ethDriver), cfg_(config), lwip_ready_(false) {
    netif_state_.driver   = &ethDriver;
    netif_state_.hostname = config.hostname;
}

// ── run() — entry point of the FreeRTOS task ──────────────────────────────
void NetManager::run() {
    LOG_INFO("NetManager: starting\r\n");

    // 1. Initialise the MAC and PHY hardware
    if (!eth_.Init()) {
        LOG_ERR("NetManager: ETH Init failed — task stopped\r\n");
        vTaskDelete(nullptr);
        return;
    }

    LOG_INFO("NetManager: PHY ID = 0x%08lX\r\n", eth_.GetPhyId());

    // 2. Wait for physical link
    LOG_INFO("NetManager: waiting for physical link...\r\n");
    while (!eth_.WaitForLink(1000)) {
        LOG_INFO("NetManager: waiting for link...\r\n");
    }
    LOG_INFO("NetManager: link UP\r\n");

    // 3. Initialise lwIP + add netif (runs on_tcpip_init_done inside tcpip_thread)
    LOG_DBG("NetManager: calling tcpip_init...\r\n");
    tcpip_init(on_tcpip_init_done, this);

    // Wait for initLwip to complete in tcpip_thread context
    while (!lwip_ready_) {
        vTaskDelay(pdMS_TO_TICKS(10));
    }
    LOG_DBG("NetManager: lwIP init completed successfully\r\n");

    // 4. Monitor link status and DHCP assignment
    monitorLoop();
}

// ── initLwip() — called inside tcpip_thread context ──────────────────────
void NetManager::initLwip() {
    LOG_DBG("NetManager: [tcpip_thread] initLwip starting...\r\n");
    ip4_addr_t ipaddr, netmask, gw;

    if (cfg_.mode == IpMode::STATIC) {
        ipaddr.addr  = cfg_.static_ip;
        netmask.addr = cfg_.netmask;
        gw.addr      = cfg_.gateway;
    } else {
        ip_addr_set_zero_ip4(&ipaddr);
        ip_addr_set_zero_ip4(&netmask);
        ip_addr_set_zero_ip4(&gw);
    }

    netif_.state = &netif_state_;

    netif_add(&netif_, &ipaddr, &netmask, &gw,
              &netif_state_,
              ethernetif_init,
              tcpip_input);

    netif_set_default(&netif_);
    netif_set_link_up(&netif_);
    netif_set_up(&netif_);

    if (cfg_.mode == IpMode::STATIC) {
        applyStaticIpInternal();
    } else {
        LOG_INFO("NetManager: starting DHCP\r\n");
        dhcp_start(&netif_);
    }

    lwip_ready_ = true;
    LOG_DBG("NetManager: [tcpip_thread] initLwip finished\r\n");
}

// ── applyStaticIpInternal() — MUST be called inside tcpip_thread context ─
void NetManager::applyStaticIpInternal() {
    ip4_addr_t ipaddr, netmask, gw;
    ipaddr.addr  = cfg_.static_ip;
    netmask.addr = cfg_.netmask;
    gw.addr      = cfg_.gateway;
    netif_set_addr(&netif_, &ipaddr, &netmask, &gw);
    LOG_INFO("NetManager: static IP %u.%u.%u.%u\r\n",
             (unsigned)((cfg_.static_ip)       & 0xFF),
             (unsigned)((cfg_.static_ip >> 8)  & 0xFF),
             (unsigned)((cfg_.static_ip >> 16) & 0xFF),
             (unsigned)((cfg_.static_ip >> 24) & 0xFF));
}

// ── waitForDhcpLease() ─────────────────────────────────────────────────────
bool NetManager::waitForDhcpLease(uint32_t timeout_ms) {
    uint32_t elapsed = 0;
    while (elapsed < timeout_ms) {
        // Poll RX DMA descriptors so incoming DHCP OFFER/ACK packets reach lwIP
        ethernetif_input(&netif_);

        if (dhcp_supplied_address(&netif_)) {
            return true;
        }
        vTaskDelay(pdMS_TO_TICKS(10));
        elapsed += 10;
    }
    return false;
}

// ── monitorLoop() ──────────────────────────────────────────────────────────
void NetManager::monitorLoop() {
    // ── DHCP / Fallback handling ───────────────────────────────────────────
    if (cfg_.mode == IpMode::DHCP_WITH_FALLBACK) {
        LOG_INFO("NetManager: waiting up to %lums for DHCP lease\r\n",
                 cfg_.dhcp_timeout_ms);
        if (!waitForDhcpLease(cfg_.dhcp_timeout_ms)) {
            LOG_INFO("NetManager: DHCP timeout — applying static fallback\r\n");
            err_t err = tcpip_callback(cb_fallback_static, this);
            if (err != ERR_OK) {
                LOG_ERR("NetManager: tcpip_callback fallback failed err=%d\r\n", (int)err);
            }
            vTaskDelay(pdMS_TO_TICKS(100));
        } else {
            LOG_INFO("NetManager: DHCP IP = %s\r\n",
                     ip4addr_ntoa(netif_ip4_addr(&netif_)));
            etharp_gratuitous(&netif_);
        }
    }

    // ── Main monitor loop ──────────────────────────────────────────────────
    bool dhcp_printed = false;
    bool link_was_up  = true;
    TickType_t last_link_check = xTaskGetTickCount();

    while (true) {
        // ── Read RX DMA descriptors immediately (1ms polling latency) ───────
        ethernetif_input(&netif_);

        TickType_t now = xTaskGetTickCount();
        if ((now - last_link_check) >= pdMS_TO_TICKS(500)) {
            last_link_check = now;

            // ── DHCP mode: print IP once acquired ──────────────────────────
            if (cfg_.mode == IpMode::DHCP && !dhcp_printed) {
                if (dhcp_supplied_address(&netif_)) {
                    LOG_INFO("NetManager: DHCP IP = %s\r\n",
                             ip4addr_ntoa(netif_ip4_addr(&netif_)));
                    dhcp_printed = true;
                }
            }

            // ── Link loss detection and recovery ───────────────────────────
            bool link_now_up = eth_.IsLinkUp();

            if (link_was_up && !link_now_up) {
                // Link just dropped
                LOG_INFO("NetManager: link DOWN\r\n");
                tcpip_callback(cb_link_down, this);
                dhcp_printed = false;
                link_was_up  = false;
            }

            if (!link_was_up && link_now_up) {
                // Link just recovered
                LOG_INFO("NetManager: link restored\r\n");
                tcpip_callback(cb_link_up, this);
                if (cfg_.mode == IpMode::DHCP_WITH_FALLBACK) {
                    if (!waitForDhcpLease(cfg_.dhcp_timeout_ms)) {
                        LOG_INFO("NetManager: DHCP timeout — applying static fallback\r\n");
                        tcpip_callback(cb_fallback_static, this);
                        vTaskDelay(pdMS_TO_TICKS(100));
                    } else {
                        LOG_INFO("NetManager: DHCP IP = %s\r\n",
                                 ip4addr_ntoa(netif_ip4_addr(&netif_)));
                    }
                }
                link_was_up = true;
            }
        }

        vTaskDelay(pdMS_TO_TICKS(1));
    }
}

} // namespace net
