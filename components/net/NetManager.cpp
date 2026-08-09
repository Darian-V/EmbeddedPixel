#include "NetManager.h"
#include "console.h"
#include "net_config.h"

// lwIP includes
#include "lwip/init.h"
#include "lwip/netif.h"
#include "lwip/tcpip.h"
#include "lwip/dhcp.h"
#include "ethernetif.h"

namespace net {

struct netif gnetif;

static void tcpip_init_done(void *arg) {
    printf("lwIP: tcpip_thread started\r\n");

    ip4_addr_t ipaddr;
    ip4_addr_t netmask;
    ip4_addr_t gw;

#if NET_USE_DHCP
    ip_addr_set_zero_ip4(&ipaddr);
    ip_addr_set_zero_ip4(&netmask);
    ip_addr_set_zero_ip4(&gw);
#else
    IP4_ADDR(&ipaddr, 192, 168, 1, 100);
    IP4_ADDR(&netmask, 255, 255, 255, 0);
    IP4_ADDR(&gw, 192, 168, 1, 1);
#endif

    // Add network interface
    netif_add(&gnetif, &ipaddr, &netmask, &gw, NULL, &ethernetif_init, &tcpip_input);
    netif_set_default(&gnetif);

    // We already verified the physical link is UP via eth.WaitForLink()
    netif_set_link_up(&gnetif);
    netif_set_up(&gnetif);

#if NET_USE_DHCP
    printf("NetManager: Starting DHCP...\r\n");
    dhcp_start(&gnetif);
#endif
}

NetManager::NetManager(IEth& ethDriver) : eth(ethDriver) {
}

void NetManager::run() {
    printf("NetManager: Starting...\r\n");

    if (!eth.Init()) {
        printf("NetManager: ETH Init failed!\r\n");
        while (1) {
            // Delay or retry
        }
    }

    uint32_t phyId = eth.GetPhyId();
    printf("NetManager: PHY ID = 0x%08X\r\n", (unsigned int)phyId);

    printf("NetManager: Waiting for link...\r\n");
    while (!eth.WaitForLink(1000)) {
        printf(".");
    }
    printf("\r\nNetManager: Link UP!\r\n");

    // Initialize lwIP tcpip thread and setup netif inside its context
    tcpip_init(tcpip_init_done, NULL);

    // Monitor link and DHCP status
    while (1) {
        // Here you would check PHY link status via eth.IsLinkUp() 
        // and handle cable plug/unplug, and print DHCP assigned IP.
        
#if NET_USE_DHCP
        if (dhcp_supplied_address(&gnetif)) {
            static bool printed = false;
            if (!printed) {
                printf("NetManager: DHCP IP Assigned: %s\r\n", ip4addr_ntoa(netif_ip4_addr(&gnetif)));
                printed = true;
            }
        }
#endif
        ethernetif_input(&gnetif);
        vTaskDelay(10); // Polling delay
    }
}

} // namespace net
