#include "Stm32H7Can.h"
#include <cstring>

namespace stm32::h7 {

Stm32H7Can::Stm32H7Can(FDCAN_GlobalTypeDef* instance) {
    hfdcan_ = {};
    hfdcan_.Instance = instance;
}

Stm32H7Can::~Stm32H7Can() {
    if (initialized_) {
        HAL_FDCAN_Stop(&hfdcan_);
        HAL_FDCAN_DeInit(&hfdcan_);
    }
}

void Stm32H7Can::configure_bit_timing(hal::CanBaudRate baud) {
    // Assuming FDCAN kernel clock is 100 MHz (PLL2P @ 200MHz / DIV2 or PLL1Q / DIV3)
    // Bit Timing = (1 + TimeSeg1 + TimeSeg2) * Prescaler
    // With 20 TQ (TimeSeg1 = 15, TimeSeg2 = 4, SyncJump = 4, Sample point = 80%):
    // Prescaler = 100MHz / (Baud * 20)
    uint32_t bitrate = static_cast<uint32_t>(baud);
    uint32_t prescaler = 10;

    switch (bitrate) {
        case 1000000: prescaler = 5;  break; // 1 Mbps: 100M / (1M * 20) = 5
        case 800000:  prescaler = 6;  break; // 800 kbps (approx / custom TQ)
        case 500000:  prescaler = 10; break; // 500 kbps: 100M / (500k * 20) = 10
        case 250000:  prescaler = 20; break; // 250 kbps: 100M / (250k * 20) = 20
        case 125000:  prescaler = 40; break; // 125 kbps: 100M / (125k * 20) = 40
        default:      prescaler = 10; break;
    }

    hfdcan_.Init.NominalPrescaler = prescaler;
    hfdcan_.Init.NominalSyncJumpWidth = 4;
    hfdcan_.Init.NominalTimeSeg1 = 15;
    hfdcan_.Init.NominalTimeSeg2 = 4;
}

bool Stm32H7Can::init(hal::CanBaudRate baud) {
    hfdcan_.Init.FrameFormat = FDCAN_FRAME_CLASSIC;
    hfdcan_.Init.Mode = FDCAN_MODE_NORMAL;
    hfdcan_.Init.AutoRetransmission = ENABLE;
    hfdcan_.Init.TransmitPause = DISABLE;
    hfdcan_.Init.ProtocolException = DISABLE;

    configure_bit_timing(baud);

    // Data bit timing (unused in Classic CAN, but initialized for completeness)
    hfdcan_.Init.DataPrescaler = 1;
    hfdcan_.Init.DataSyncJumpWidth = 1;
    hfdcan_.Init.DataTimeSeg1 = 1;
    hfdcan_.Init.DataTimeSeg2 = 1;

    hfdcan_.Init.StdFiltersNbr = 0;
    hfdcan_.Init.ExtFiltersNbr = 1;
    hfdcan_.Init.TxFifoQueueMode = FDCAN_TX_FIFO_OPERATION;

    if (HAL_FDCAN_Init(&hfdcan_) != HAL_OK) {
        return false;
    }

    // Accept all standard and extended frames into Rx FIFO 0
    if (HAL_FDCAN_ConfigGlobalFilter(&hfdcan_,
                                     FDCAN_ACCEPT_IN_RX_FIFO0,
                                     FDCAN_ACCEPT_IN_RX_FIFO0,
                                     FDCAN_FILTER_REMOTE,
                                     FDCAN_FILTER_REMOTE) != HAL_OK) {
        return false;
    }

    if (HAL_FDCAN_Start(&hfdcan_) != HAL_OK) {
        return false;
    }

    initialized_ = true;
    return true;
}

bool Stm32H7Can::transmit(const hal::CanFrame& frame, uint32_t timeout_ms) {
    if (!initialized_) {
        return false;
    }

    FDCAN_TxHeaderTypeDef txHeader{};
    txHeader.Identifier = frame.id;
    txHeader.IdType = frame.is_extended ? FDCAN_EXTENDED_ID : FDCAN_STANDARD_ID;
    txHeader.TxFrameType = frame.is_rtr ? FDCAN_REMOTE_FRAME : FDCAN_DATA_FRAME;
    txHeader.DataLength = (frame.dlc <= 8) ? frame.dlc : 8;
    txHeader.ErrorStateIndicator = FDCAN_ESI_ACTIVE;
    txHeader.BitRateSwitch = FDCAN_BRS_OFF;
    txHeader.FDFormat = FDCAN_CLASSIC_CAN;
    txHeader.TxEventFifoControl = FDCAN_NO_TX_EVENTS;
    txHeader.MessageMarker = 0;

    uint32_t start_tick = HAL_GetTick();
    while (HAL_FDCAN_GetTxFifoFreeLevel(&hfdcan_) == 0) {
        if ((HAL_GetTick() - start_tick) >= timeout_ms) {
            return false;
        }
    }

    return HAL_FDCAN_AddMessageToTxFifoQ(&hfdcan_, &txHeader, const_cast<uint8_t*>(frame.data)) == HAL_OK;
}

bool Stm32H7Can::receive(hal::CanFrame& frame, uint32_t timeout_ms) {
    if (!initialized_) {
        return false;
    }

    uint32_t start_tick = HAL_GetTick();
    while (HAL_FDCAN_GetRxFifoFillLevel(&hfdcan_, FDCAN_RX_FIFO0) == 0) {
        if (timeout_ms == 0 || (HAL_GetTick() - start_tick) >= timeout_ms) {
            return false;
        }
    }

    FDCAN_RxHeaderTypeDef rxHeader{};
    if (HAL_FDCAN_GetRxMessage(&hfdcan_, FDCAN_RX_FIFO0, &rxHeader, frame.data) != HAL_OK) {
        return false;
    }

    frame.id = rxHeader.Identifier;
    frame.is_extended = (rxHeader.IdType == FDCAN_EXTENDED_ID);
    frame.is_rtr = (rxHeader.RxFrameType == FDCAN_REMOTE_FRAME);
    frame.dlc = (rxHeader.DataLength <= 8) ? static_cast<uint8_t>(rxHeader.DataLength) : 8;
    frame.timestamp_us = rxHeader.RxTimestamp;
    return true;
}

bool Stm32H7Can::configure_filter(const hal::CanFilter& filter) {
    if (!initialized_) {
        return false;
    }

    FDCAN_FilterTypeDef sFilterConfig{};
    sFilterConfig.IdType = filter.is_extended ? FDCAN_EXTENDED_ID : FDCAN_STANDARD_ID;
    sFilterConfig.FilterIndex = 0;
    sFilterConfig.FilterType = FDCAN_FILTER_MASK;
    sFilterConfig.FilterConfig = (filter.fifo_assignment == 1) ? FDCAN_FILTER_TO_RXFIFO1 : FDCAN_FILTER_TO_RXFIFO0;
    sFilterConfig.FilterID1 = filter.id;
    sFilterConfig.FilterID2 = filter.mask;

    return HAL_FDCAN_ConfigFilter(&hfdcan_, &sFilterConfig) == HAL_OK;
}

bool Stm32H7Can::is_bus_off() const {
    if (!initialized_) {
        return false;
    }
    FDCAN_ProtocolStatusTypeDef protocolStatus{};
    if (HAL_FDCAN_GetProtocolStatus(const_cast<FDCAN_HandleTypeDef*>(&hfdcan_), &protocolStatus) == HAL_OK) {
        return protocolStatus.BusOff != 0;
    }
    return false;
}

void Stm32H7Can::recover_bus() {
    if (initialized_) {
        HAL_FDCAN_Stop(&hfdcan_);
        HAL_FDCAN_Start(&hfdcan_);
    }
}

} // namespace stm32::h7
