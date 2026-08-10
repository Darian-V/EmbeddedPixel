#pragma once

#include "FreeRtosThread.h"   // for osal::Runnable
#include "IEth.h"
#include "lwip/netif.h"

namespace net {

/**
 * @brief IP address configuration mode.
 */
enum class IpMode : uint8_t {
    STATIC,             ///< Use static_ip/netmask/gateway immediately at boot.
    DHCP,               ///< DHCP only — spin forever waiting for lease.
    DHCP_WITH_FALLBACK  ///< Try DHCP; after dhcp_timeout_ms apply static fallback.
};

/**
 * @brief Complete IP stack configuration passed to NetManager at construction.
 *
 * IP addresses are stored as lwIP ip4_addr_t compatible uint32_t values.
 * Use the IP4_MAKE() helper below to construct them.
 */
struct IpConfig {
    IpMode      mode             = IpMode::DHCP;
    uint32_t    static_ip        = 0;   ///< Used for STATIC or DHCP fallback
    uint32_t    netmask          = 0;
    uint32_t    gateway          = 0;
    uint32_t    dhcp_timeout_ms  = 10000;
    const char* hostname         = "embeddedpixel";
};

/**
 * @brief Construct a uint32_t IP address from four octets (host byte order).
 * Equivalent to IP4_ADDR macro but usable in a constant expression.
 */
constexpr uint32_t IP4_MAKE(uint8_t a, uint8_t b, uint8_t c, uint8_t d) {
    return ((uint32_t)a) |
           ((uint32_t)b << 8)  |
           ((uint32_t)c << 16) |
           ((uint32_t)d << 24);
}

/**
 * @brief Network manager task.
 *
 * Runs as a FreeRTOS task (via osal::Runnable). Initialises lwIP,
 * adds a netif, manages DHCP/static IP assignment, and monitors for
 * link-loss events with automatic recovery.
 */
class NetManager : public osal::Runnable {
public:
    /**
     * @param ethDriver  Reference to the MAC driver. Must outlive NetManager.
     * @param config     IP configuration. Copied at construction time.
     */
    NetManager(IEth& ethDriver, const IpConfig& config);
    ~NetManager() = default;

    void run() override;

    // Helpers called from tcpip_thread context
    void initLwip();
    void applyStaticIpInternal();

    struct netif& get_netif() { return netif_; }
    const IpConfig& config() const { return cfg_; }

private:
    IEth&    eth_;
    IpConfig cfg_;
    volatile bool lwip_ready_;

    // lwIP structures — owned by this class
    struct netif  netif_;
    struct {
        IEth*       driver;
        const char* hostname;
    } netif_state_;

    // Internal helpers
    bool waitForDhcpLease(uint32_t timeout_ms);
    void monitorLoop();
};

} // namespace net
