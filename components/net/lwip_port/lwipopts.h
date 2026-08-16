#ifndef LWIPOPTS_H
#define LWIPOPTS_H

#define NO_SYS                  0       // Use OS (FreeRTOS)
#define MEM_ALIGNMENT           4       // 4-byte memory alignment for ARM Cortex-M7
#define LWIP_NETCONN            1       // Enable Netconn API
#define LWIP_SOCKET             0       // Disable BSD sockets (save RAM)
#define MEM_SIZE                (32 * 1024)  // 32 KB lwIP heap
#define MEMP_NUM_PBUF           32
#define MEMP_NUM_NETCONN        16
#define MEMP_NUM_NETBUF         16
#define MEMP_NUM_UDPPCB         8
#define MEMP_NUM_TCP_PCB        8
#define MEMP_NUM_TCP_PCB_LISTEN 4
#define MEMP_NUM_SYS_TIMEOUT    16
#define PBUF_POOL_SIZE          24
#define PBUF_POOL_BUFSIZE       1536
#define TCP_MSS                 1460
#define TCP_SND_BUF             (4 * TCP_MSS)
#define TCP_WND                 (4 * TCP_MSS)
#define LWIP_DHCP               1       // Enable DHCP
#define TCPIP_THREAD_STACKSIZE  4096    // Stack size for lwIP tcpip_thread
#define TCPIP_THREAD_PRIO       5       // RTOS_PRIORITY_REALTIME
#define DEFAULT_THREAD_STACKSIZE 1024

#define TCPIP_MBOX_SIZE         16
#define DEFAULT_RAW_RECVMBOX_SIZE 16
#define DEFAULT_UDP_RECVMBOX_SIZE 16
#define DEFAULT_TCP_RECVMBOX_SIZE 16
#define DEFAULT_ACCEPTMBOX_SIZE 16

#define LWIP_SO_RCVTIMEO        1
#define LWIP_SO_SNDTIMEO        1
#define LWIP_SO_RCVBUF          1

// Feature flags
#define LWIP_IPV4               1
#define LWIP_ICMP               1
#define LWIP_PROVIDE_ERRNO      1
#define LWIP_STATS              0
#define IP_REASSEMBLY           0
#define IP_FRAG                 0
#define LWIP_IPV4_REASS         0
#define LWIP_IPV4_FRAG          0
#define LWIP_ACD                0
#define LWIP_DHCP_DOES_ACD_CHECK 0

// Disable software checksum checking for incoming packets (routers/NICs offload or alter checksums)
#define CHECKSUM_CHECK_IP       0
#define CHECKSUM_CHECK_UDP      0
#define CHECKSUM_CHECK_TCP      0

// Debug logging (0 = clean level 2 output)
#define LWIP_DEBUG              0
#define DHCP_DEBUG              LWIP_DBG_OFF
#define UDP_DEBUG               LWIP_DBG_OFF
#define IP_DEBUG                LWIP_DBG_OFF
#define NETIF_DEBUG             LWIP_DBG_OFF

#endif // LWIPOPTS_H
