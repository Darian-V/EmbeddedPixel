#pragma once

#include <stdint.h>
#include <stdbool.h>

/**
 * @brief Abstract interface for an Ethernet MAC driver.
 *
 * This interface covers only MAC-level operations (init, TX, RX, link state).
 * PHY operations (reset, link polling, speed/duplex decode) are handled by IPhy.
 */
class IEth {
public:
    virtual ~IEth() = default;

    /**
     * @brief Initialize the Ethernet MAC hardware and the injected PHY.
     * Configures DMA descriptors, MPU, clocks, and calls IPhy::Init().
     * Does NOT start the MAC — that happens in WaitForLink() once the
     * RMII clock is stable.
     * @return true if successful, false otherwise.
     */
    virtual bool Init() = 0;

    /**
     * @brief Poll until PHY link is up or timeout expires.
     * Once the link is confirmed, configures MAC speed/duplex from
     * IPhy::GetLinkConfig() and calls HAL_ETH_Start().
     * @param timeout_ms Maximum time to wait in milliseconds.
     * @return true if link came up within the timeout.
     */
    virtual bool WaitForLink(uint32_t timeout_ms) = 0;

    /**
     * @brief Check whether the physical link is currently up.
     * Used by NetManager for link-loss detection in the monitor loop.
     * @return true if link is up.
     */
    virtual bool IsLinkUp() = 0;

    /**
     * @brief Process received packets and forward them to lwIP.
     * Called from the NetManager polling loop. Internally calls
     * HAL_ETH_ReadData() and delivers each pbuf to netif->input().
     */
    virtual void ProcessRx() = 0;

    /**
     * @brief Transmit a single lwIP pbuf chain.
     * @param p Pointer to the pbuf to transmit.
     * @return true if the packet was handed to DMA successfully.
     */
    virtual bool Transmit(struct pbuf* p) = 0;

    /**
     * @brief Copy the 6-byte MAC address into the provided buffer.
     * @param mac_addr Caller-allocated buffer of at least 6 bytes.
     */
    virtual void GetMacAddress(uint8_t* mac_addr) = 0;

    /**
     * @brief Return the 32-bit PHY identifier (delegated to IPhy::GetId).
     * Kept here so callers (e.g. NetManager log) don't need to know
     * about IPhy directly.
     * @return PHY ID, or 0xFFFFFFFF on failure.
     */
    virtual uint32_t GetPhyId() = 0;

    /**
     * @brief Print MAC MMC diagnostic counters.
     */
    virtual void PrintMmcCounters() = 0;
};
