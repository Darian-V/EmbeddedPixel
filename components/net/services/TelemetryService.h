#pragma once

#include "FreeRtosThread.h"
#include "NetManager.h"
#include "proto/ProtocolTypes.h"
#include "proto/PacketHelper.h"
#include "lwip/ip_addr.h"
#include "lwip/api.h"
#include "lwip/netbuf.h"

#include "ITelemetryChannel.h"

namespace net::services {

/**
 * @brief Modular Multi-Stream UDP engine supporting per-channel rate control and scheduling.
 */
class TelemetryService : public osal::Runnable {
public:
    static constexpr size_t MAX_CHANNELS = 8;

    TelemetryService(NetManager& netManager, uint16_t nodeId);
    TelemetryService(NetManager& netManager, uint16_t nodeId, ITelemetryChannel& defaultChannel);
    ~TelemetryService() = default;

    /**
     * @brief Register a telemetry channel with this service.
     */
    bool register_channel(ITelemetryChannel& channel);

    /**
     * @brief Find a registered channel by its 4-character FourCC tag.
     */
    ITelemetryChannel* find_channel(uint32_t streamTag);

    void run() override;

    /**
     * @brief Start streaming to the specified destination host.
     * @param destIp Target IP address (unicast or broadcast).
     * @param destPort Target UDP port (default proto::PORT_STREAM 50001).
     * @param streamTag Specific FourCC tag to start (0 = all registered channels).
     * @param sampleRateHz Optional sample rate override (0 = keep channel default).
     * @param batchCount Optional batch count override (0 = keep channel default).
     */
    void start_streaming(const ip_addr_t& destIp,
                         uint16_t destPort = proto::PORT_STREAM,
                         uint32_t streamTag = 0,
                         uint16_t sampleRateHz = 0,
                         uint16_t batchCount = 0);

    /**
     * @brief Stop active data streaming.
     * @param streamTag Specific FourCC tag to stop (0 = stop all).
     */
    void stop_streaming(uint32_t streamTag = 0);

    bool is_streaming() const { return is_streaming_; }
    uint32_t get_packets_sent() const { return packets_sent_; }
    uint32_t get_samples_sent() const { return samples_sent_; }
    size_t get_channel_count() const { return channel_count_; }
    ITelemetryChannel* get_channel_at(size_t index) const {
        if (index < channel_count_) return channels_[index].channel;
        return nullptr;
    }

private:
    struct ChannelSlot {
        ITelemetryChannel* channel;
        TickType_t         last_wake_tick;
    };

    NetManager&   net_;
    uint16_t      node_id_;
    ChannelSlot   channels_[MAX_CHANNELS];
    size_t        channel_count_;
    volatile bool is_streaming_;
    ip_addr_t     dest_ip_;
    uint16_t      dest_port_;
    uint32_t      seq_num_;
    uint32_t      packets_sent_;
    uint32_t      samples_sent_;

    void sendChannelFrame(struct netconn* conn, ITelemetryChannel& channel);
};

} // namespace net::services
