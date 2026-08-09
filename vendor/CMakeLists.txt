# FreeRTOS Kernel build configuration

set(FREERTOS_DIR ${CMAKE_CURRENT_SOURCE_DIR}/FreeRTOS-Kernel)

# Exclude FreeRTOS compilation for mock target
if (NOT ${TARGET} STREQUAL "mock")

    # Add FreeRTOS sources
    target_sources(${EXECUTABLE_NAME} PRIVATE
        ${FREERTOS_DIR}/tasks.c
        ${FREERTOS_DIR}/queue.c
        ${FREERTOS_DIR}/list.c
        ${FREERTOS_DIR}/timers.c
        
        # GCC ARM CM7 Port
        ${FREERTOS_DIR}/portable/GCC/ARM_CM7/r0p1/port.c
        
        # Heap implementation (using heap_4 which is robust and supports fragmentation)
        ${FREERTOS_DIR}/portable/MemMang/heap_4.c
    )

    # Add FreeRTOS include directories
    target_include_directories(${EXECUTABLE_NAME} PRIVATE
        ${FREERTOS_DIR}/include
        ${FREERTOS_DIR}/portable/GCC/ARM_CM7/r0p1
    )

endif()

# lwIP library
if (NOT ${TARGET} STREQUAL "mock")
    set(LWIP_DIR ${CMAKE_CURRENT_SOURCE_DIR}/lwip)
    
    add_library(lwip STATIC
        ${LWIP_DIR}/src/core/init.c
        ${LWIP_DIR}/src/core/def.c
        ${LWIP_DIR}/src/core/mem.c
        ${LWIP_DIR}/src/core/memp.c
        ${LWIP_DIR}/src/core/pbuf.c
        ${LWIP_DIR}/src/core/tcp.c
        ${LWIP_DIR}/src/core/tcp_in.c
        ${LWIP_DIR}/src/core/tcp_out.c
        ${LWIP_DIR}/src/core/udp.c
        ${LWIP_DIR}/src/core/ip.c
        ${LWIP_DIR}/src/core/inet_chksum.c
        ${LWIP_DIR}/src/core/netif.c
        ${LWIP_DIR}/src/core/timeouts.c
        ${LWIP_DIR}/src/core/ipv4/dhcp.c
        ${LWIP_DIR}/src/core/ipv4/etharp.c
        ${LWIP_DIR}/src/core/ipv4/ip4.c
        ${LWIP_DIR}/src/core/ipv4/ip4_addr.c
        ${LWIP_DIR}/src/core/ipv4/icmp.c
        ${LWIP_DIR}/src/netif/ethernet.c
        ${LWIP_DIR}/src/api/api_lib.c
        ${LWIP_DIR}/src/api/api_msg.c
        ${LWIP_DIR}/src/api/netbuf.c
        ${LWIP_DIR}/src/api/netifapi.c
        ${LWIP_DIR}/src/api/tcpip.c
        ${LWIP_DIR}/src/api/err.c
        ${LWIP_DIR}/src/api/sockets.c
        ${CMAKE_SOURCE_DIR}/targets/${TARGET}/lwip_port/sys_arch.cpp
        ${CMAKE_SOURCE_DIR}/targets/${TARGET}/lwip_port/ethernetif.cpp
    )

    target_include_directories(lwip PUBLIC
        ${LWIP_DIR}/src/include
        ${CMAKE_SOURCE_DIR}/targets/${TARGET}/lwip_port
        ${CMAKE_SOURCE_DIR}/targets/${TARGET}
        ${CMAKE_SOURCE_DIR}/core/hal
        ${FREERTOS_DIR}/include
        ${FREERTOS_DIR}/portable/GCC/ARM_CM7/r0p1
    )

    target_link_libraries(${EXECUTABLE_NAME} PRIVATE lwip)
endif()
