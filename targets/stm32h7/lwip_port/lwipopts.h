#ifndef LWIPOPTS_H
#define LWIPOPTS_H

#define NO_SYS                  0       // Use OS (FreeRTOS)
#define LWIP_NETCONN            1       // Enable Netconn API
#define LWIP_SOCKET             0       // Disable BSD sockets (save RAM)
#define MEM_SIZE                (16 * 1024)  // 16 KB lwIP heap
#define MEMP_NUM_PBUF           16
#define PBUF_POOL_SIZE          16
#define PBUF_POOL_BUFSIZE       1536
#define TCP_MSS                 1460
#define TCP_SND_BUF             (4 * TCP_MSS)
#define TCP_WND                 (4 * TCP_MSS)
#define LWIP_DHCP               1       // Enable DHCP
#define TCPIP_THREAD_STACKSIZE  2048
#define TCPIP_THREAD_PRIO       5       // RTOS_PRIORITY_REALTIME
#define DEFAULT_THREAD_STACKSIZE 1024

#define TCPIP_MBOX_SIZE         16
#define DEFAULT_RAW_RECVMBOX_SIZE 16
#define DEFAULT_UDP_RECVMBOX_SIZE 16
#define DEFAULT_TCP_RECVMBOX_SIZE 16
#define DEFAULT_ACCEPTMBOX_SIZE 16

// Other necessary lwIP features can be enabled here.
#define LWIP_IPV4               1
#define LWIP_ICMP               1
#define LWIP_PROVIDE_ERRNO 1
#define LWIP_STATS 0
#define IP_REASSEMBLY 0
#define IP_FRAG 0
#define LWIP_IPV4_REASS 0
#define LWIP_IPV4_FRAG 0
#define LWIP_ACD 0
#define LWIP_DHCP_DOES_ACD_CHECK 0

#endif // LWIPOPTS_H
