#ifndef IETH_H
#define IETH_H

#include <stdint.h>
#include <stdbool.h>

class IEth {
public:
    virtual ~IEth() = default;

    /**
     * @brief Initialize the Ethernet MAC and PHY hardware.
     * @return true if successful, false otherwise.
     */
    virtual bool Init() = 0;

    /**
     * @brief Check if the physical link is up.
     * @return true if link is up, false otherwise.
     */
    virtual bool IsLinkUp() = 0;

    /**
     * @brief Check for received packets and process them.
     * This is usually called from an RTOS task to hand off packets to lwIP.
     */
    virtual void ProcessRx() = 0;

    /**
     * @brief Get the MAC address assigned to this interface.
     * @param mac_addr Buffer to store the 6-byte MAC address.
     */
    virtual void GetMacAddress(uint8_t* mac_addr) = 0;

    /**
     * @brief Transmit a packet.
     * @param p Pointer to the lwIP pbuf to transmit.
     * @return true if successful, false otherwise.
     */
    virtual bool Transmit(struct pbuf *p) = 0;

    /**
     * @brief Get the PHY ID.
     * @return The PHY ID value.
     */
    virtual uint32_t GetPhyId() = 0;

    /**
     * @brief Wait for the link to be established.
     * @param timeout_ms Timeout in milliseconds.
     * @return true if link is up, false otherwise.
     */
    virtual bool WaitForLink(uint32_t timeout_ms) = 0;

    /**
     * @brief Print the MAC MMC Receive Counters
     */
    virtual void PrintMmcCounters() = 0;
};

#endif // IETH_H
