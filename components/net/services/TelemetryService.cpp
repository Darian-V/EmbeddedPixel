#include "TelemetryService.h"
#include "net_log.h"

// lwIP
#include "lwip/api.h"

// FreeRTOS
#include "FreeRTOS.h"
#include "task.h"

namespace net::services {

TelemetryService::TelemetryService(NetManager& netManager, uint16_t nodeId)
    : net_(netManager),
      node_id_(nodeId),
      is_streaming_(false),
      dest_port_(proto::PORT_STREAM),
      sample_rate_hz_(10000),
      batch_count_(50),
      seq_num_(0),
      packets_sent_(0),
      samples_sent_(0) {
    ip_addr_set_zero(&dest_ip_);
}

void TelemetryService::start_streaming(const ip_addr_t& destIp,
                                       uint16_t destPort,
                                       uint16_t sampleRateHz,
                                       uint16_t batchCount) {
    dest_ip_        = destIp;
    dest_port_      = destPort;
    sample_rate_hz_ = sampleRateHz;
    batch_count_    = (batchCount > 0 && batchCount <= 100) ? batchCount : 50;
    is_streaming_   = true;
    LOG_INFO("TelemetryService: stream started to %s:%u (Rate=%uHz, Batch=%u)\r\n",
             ipaddr_ntoa(&dest_ip_), dest_port_, sample_rate_hz_, batch_count_);
}

void TelemetryService::stop_streaming() {
    if (is_streaming_) {
        is_streaming_ = false;
        LOG_INFO("TelemetryService: stream stopped (Total packets=%lu, samples=%lu)\r\n",
                 packets_sent_, samples_sent_);
    }
}

void TelemetryService::run() {
    LOG_INFO("TelemetryService: waiting for network ready...\r\n");

    while (!net_.is_ready()) {
        vTaskDelay(pdMS_TO_TICKS(100));
    }

    LOG_INFO("TelemetryService: ready for UDP streaming\r\n");

    struct netconn* conn = nullptr;
    while (conn == nullptr) {
        conn = netconn_new(NETCONN_UDP);
        if (conn == nullptr) {
            LOG_ERR("TelemetryService: netconn_new failed, retrying...\r\n");
            vTaskDelay(pdMS_TO_TICKS(500));
        }
    }

    TickType_t last_wake_time = xTaskGetTickCount();

    while (true) {
        if (!is_streaming_ || !net_.is_ready()) {
            vTaskDelay(pdMS_TO_TICKS(50));
            last_wake_time = xTaskGetTickCount();
            continue;
        }

        // Send batched frame
        sendBatchFrame(conn);

        // Compute delay in ms based on batch size and sampling frequency
        // Interval ms = (batch_count * 1000) / sample_rate_hz
        uint32_t interval_ms = (static_cast<uint32_t>(batch_count_) * 1000) / sample_rate_hz_;
        if (interval_ms < 1) {
            interval_ms = 1;
        }

        vTaskDelayUntil(&last_wake_time, pdMS_TO_TICKS(interval_ms));
    }
}

void TelemetryService::sendBatchFrame(struct netconn* conn) {
    constexpr uint16_t CHANNEL_COUNT = 8;
    constexpr uint16_t BYTES_PER_SAMPLE = sizeof(uint16_t) * CHANNEL_COUNT; // 16 bytes per sample

    // Max 100 samples per batch
    static uint8_t tx_frame_buf[sizeof(proto::PE_Header) +
                                sizeof(proto::StreamPayloadHeader) +
                                (100 * BYTES_PER_SAMPLE)];

    uint16_t samples_to_send = (batch_count_ <= 100) ? batch_count_ : 100;
    uint16_t raw_data_bytes  = samples_to_send * BYTES_PER_SAMPLE;
    uint16_t total_payload_len = sizeof(proto::StreamPayloadHeader) + raw_data_bytes;

    auto* hdr = reinterpret_cast<proto::PE_Header*>(tx_frame_buf);
    auto* stream_hdr = reinterpret_cast<proto::StreamPayloadHeader*>(
        tx_frame_buf + sizeof(proto::PE_Header)
    );
    auto* sample_data = reinterpret_cast<uint16_t*>(
        tx_frame_buf + sizeof(proto::PE_Header) + sizeof(proto::StreamPayloadHeader)
    );

    // Populate stream metadata
    stream_hdr->timestamp_us    = static_cast<uint64_t>(xTaskGetTickCount()) * 1000;
    stream_hdr->sample_rate_hz  = sample_rate_hz_;
    stream_hdr->sample_count    = samples_to_send;
    stream_hdr->channel_count   = CHANNEL_COUNT;
    stream_hdr->sample_type     = static_cast<uint16_t>(proto::SampleType::UINT16);

    // Populate sample payload (e.g. synthetic ramp/test pattern or sensor DMA read)
    static uint16_t ramp_counter = 0;
    for (uint16_t s = 0; s < samples_to_send; ++s) {
        for (uint16_t ch = 0; ch < CHANNEL_COUNT; ++ch) {
            sample_data[s * CHANNEL_COUNT + ch] = ramp_counter + ch;
        }
        ramp_counter++;
    }

    // Populate common protocol header
    proto::PacketHelper::PopulateHeader(
        *hdr,
        node_id_,
        proto::MessageType::STREAM_SENSOR_BATCH,
        ++seq_num_,
        total_payload_len,
        proto::FLAG_HAS_TIMESTAMP,
        false
    );

    struct netbuf* buf = netbuf_new();
    if (buf != nullptr) {
        uint16_t total_datagram_size = sizeof(proto::PE_Header) + total_payload_len;
        netbuf_ref(buf, tx_frame_buf, total_datagram_size);
        netconn_sendto(conn, buf, &dest_ip_, dest_port_);
        netbuf_delete(buf);

        packets_sent_++;
        samples_sent_ += samples_to_send;
    }
}

} // namespace net::services
