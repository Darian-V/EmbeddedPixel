#pragma once

#include "FreeRtosThread.h"
#include "NetManager.h"
#include "proto/ProtocolTypes.h"
#include "proto/PacketHelper.h"
#include "lwip/ip_addr.h"
#include "lwip/api.h"
#include "lwip/netbuf.h"

namespace net::services {

/**
 * @brief High-speed UDP streaming engine for batched sensor data up to 10 kHz.
 */
class TelemetryService : public osal::Runnable {
public:
    TelemetryService(NetManager& netManager, uint16_t nodeId);
    ~TelemetryService() = default;

    void run() override;

    /**
     * @brief Start streaming data to the specified destination host.
     */
    void start_streaming(const ip_addr_t& destIp,
                         uint16_t destPort = proto::PORT_STREAM,
                         uint16_t sampleRateHz = 10000,
                         uint16_t batchCount = 50);

    /**
     * @brief Stop active data streaming.
     */
    void stop_streaming();

    bool is_streaming() const { return is_streaming_; }
    uint32_t get_packets_sent() const { return packets_sent_; }
    uint32_t get_samples_sent() const { return samples_sent_; }

private:
    NetManager&   net_;
    uint16_t      node_id_;
    volatile bool is_streaming_;
    ip_addr_t     dest_ip_;
    uint16_t      dest_port_;
    uint16_t      sample_rate_hz_;
    uint16_t      batch_count_;
    uint32_t      seq_num_;
    uint32_t      packets_sent_;
    uint32_t      samples_sent_;

    void sendBatchFrame(struct netconn* conn);
};

} // namespace net::services
