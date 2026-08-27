#pragma once

#include <cstdint>
#include <cstddef>

namespace hal {

/**
 * @brief Standard CAN Bitrates.
 */
enum class CanBaudRate : uint32_t {
    Baud125k = 125000,
    Baud250k = 250000,
    Baud500k = 500000,
    Baud800k = 800000,
    Baud1M   = 1000000,
};

/**
 * @brief Standard or Extended CAN 2.0B / CAN-FD Data Frame.
 */
struct CanFrame {
    uint32_t id{0};             ///< 11-bit standard ID or 29-bit extended ID
    bool     is_extended{true};  ///< True if 29-bit extended identifier
    bool     is_rtr{false};      ///< True if Remote Transmission Request
    uint8_t  dlc{0};             ///< Data Length Code (0 to 8 bytes)
    uint8_t  data[8]{0};         ///< Frame payload bytes
    uint32_t timestamp_us{0};    ///< Capture timestamp in microseconds (optional)
};

/**
 * @brief CAN Acceptance Filter Configuration.
 */
struct CanFilter {
    uint32_t id{0};             ///< Filter identifier match value
    uint32_t mask{0};           ///< Filter mask (1 = must match bit in id, 0 = ignore)
    bool     is_extended{true};  ///< Filter applies to extended (29-bit) frames
    uint8_t  fifo_assignment{0}; ///< Target Rx FIFO (0 or 1)
};

/**
 * @brief Pure virtual interface for CAN bus controllers.
 */
class ICan {
public:
    virtual ~ICan() = default;

    /**
     * @brief Initialize the CAN controller peripheral and transceiver.
     * @param baud Desired nominal CAN bitrate (default 500 kbps)
     * @return true on success, false on initialization error
     */
    virtual bool init(CanBaudRate baud = CanBaudRate::Baud500k) = 0;

    /**
     * @brief Transmit a CAN frame.
     * @param frame Reference to CanFrame to transmit
     * @param timeout_ms Timeout in milliseconds (0 for non-blocking)
     * @return true if successfully enqueued/transmitted, false on timeout/error
     */
    virtual bool transmit(const CanFrame& frame, uint32_t timeout_ms = 10) = 0;

    /**
     * @brief Receive a CAN frame from the Rx FIFO.
     * @param[out] frame Reference to CanFrame where received data will be stored
     * @param timeout_ms Timeout in milliseconds (0 for non-blocking poll)
     * @return true if a valid frame was received, false if FIFO was empty or timeout
     */
    virtual bool receive(CanFrame& frame, uint32_t timeout_ms = 0) = 0;

    /**
     * @brief Configure a hardware acceptance filter.
     * @param filter Filter parameters
     * @return true on success, false on configuration error
     */
    virtual bool configure_filter(const CanFilter& filter) = 0;

    /**
     * @brief Check if CAN controller is currently in Bus-Off state.
     * @return true if Bus-Off error detected
     */
    virtual bool is_bus_off() const = 0;

    /**
     * @brief Attempt automatic or manual recovery from Bus-Off condition.
     */
    virtual void recover_bus() = 0;
};

} // namespace hal
