#pragma once

#include <stdint.h>
#include <stdbool.h>
#include "stm32h7rsxx_hal.h"

/**
 * @brief Abstract interface for an Ethernet PHY chip.
 *
 * Implementations encapsulate all PHY-specific register knowledge.
 * The ETH_HandleTypeDef* is passed through on every call so that
 * a single IPhy instance can be used with any MAC handle.
 */
class IPhy {
public:
    virtual ~IPhy() = default;

    /**
     * @brief Soft-reset the PHY and wait for it to be ready.
     * Called by the MAC driver once after HAL_ETH_Init() succeeds.
     * @param heth Pointer to the MAC HAL handle.
     * @return true if PHY responded and initialised correctly.
     */
    virtual bool Init(ETH_HandleTypeDef* heth) = 0;

    /**
     * @brief Poll whether the physical link is established.
     * @param heth Pointer to the MAC HAL handle.
     * @return true if link is up AND auto-negotiation is complete.
     */
    virtual bool IsLinkUp(ETH_HandleTypeDef* heth) = 0;

    /**
     * @brief Return the 32-bit PHY identifier (OUI + model + rev).
     * Formed as (PHYIDR1 << 16) | PHYIDR2.
     * @param heth Pointer to the MAC HAL handle.
     * @return PHY ID, or 0xFFFFFFFF on read failure.
     */
    virtual uint32_t GetId(ETH_HandleTypeDef* heth) = 0;

    /**
     * @brief Read the negotiated link speed and duplex from the PHY.
     * @param heth Pointer to the MAC HAL handle.
     * @param speed_out  Receives HAL_ETH_SPEED_100M or HAL_ETH_SPEED_10M.
     * @param duplex_out Receives ETH_FULLDUPLEX_MODE or ETH_HALFDUPLEX_MODE.
     * @return true if values were read successfully.
     */
    virtual bool GetLinkConfig(ETH_HandleTypeDef* heth,
                               uint32_t& speed_out,
                               uint32_t& duplex_out) = 0;
};
