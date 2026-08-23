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
      channel_count_(0),
      is_streaming_(false),
      dest_port_(proto::PORT_STREAM),
      seq_num_(0),
      packets_sent_(0),
      samples_sent_(0) {
    ip_addr_set_ip4_u32(&dest_ip_, IPADDR_BROADCAST);
    for (size_t i = 0; i < MAX_CHANNELS; ++i) {
        channels_[i].channel = nullptr;
        channels_[i].last_wake_tick = 0;
    }
}

TelemetryService::TelemetryService(NetManager& netManager, uint16_t nodeId, ITelemetryChannel& defaultChannel)
    : TelemetryService(netManager, nodeId) {
    register_channel(defaultChannel);
}

bool TelemetryService::register_channel(ITelemetryChannel& channel) {
    if (channel_count_ >= MAX_CHANNELS) {
        LOG_ERR("TelemetryService: maximum channel capacity (%u) reached\r\n", MAX_CHANNELS);
        return false;
    }

    // Check for duplicate registration
    for (size_t i = 0; i < channel_count_; ++i) {
        if (channels_[i].channel == &channel || channels_[i].channel->get_tag() == channel.get_tag()) {
            return false;
        }
    }

    channels_[channel_count_].channel = &channel;
    channels_[channel_count_].last_wake_tick = xTaskGetTickCount();
    channel_count_++;

    char tag_str[5] = {0};
    uint32_t tag = channel.get_tag();
    tag_str[0] = static_cast<char>(tag & 0xFF);
    tag_str[1] = static_cast<char>((tag >> 8) & 0xFF);
    tag_str[2] = static_cast<char>((tag >> 16) & 0xFF);
    tag_str[3] = static_cast<char>((tag >> 24) & 0xFF);

    LOG_INFO("TelemetryService: registered channel '%s' [%s] (NativeRate=%uHz, Batch=%u)\r\n",
             channel.get_name(), tag_str, channel.get_sample_rate_hz(), channel.get_batch_count());
    return true;
}

ITelemetryChannel* TelemetryService::find_channel(uint32_t streamTag) {
    for (size_t i = 0; i < channel_count_; ++i) {
        if (channels_[i].channel && channels_[i].channel->get_tag() == streamTag) {
            return channels_[i].channel;
        }
    }
    return nullptr;
}

void TelemetryService::start_streaming(const ip_addr_t& destIp,
                                       uint16_t destPort,
                                       uint32_t streamTag,
                                       uint16_t sampleRateHz,
                                       uint16_t batchCount) {
    dest_ip_   = destIp;
    dest_port_ = destPort;

    if (streamTag != 0) {
        ITelemetryChannel* ch = find_channel(streamTag);
        if (ch != nullptr) {
            ch->set_enabled(true);
            if (sampleRateHz > 0) {
                ch->set_sample_rate_hz(sampleRateHz);
            }
            if (batchCount > 0 && sampleRateHz > 100) {
                ch->set_batch_count(batchCount);
            }
            LOG_INFO("TelemetryService: stream '%s' started to %s:%u (Rate=%uHz, Batch=%u)\r\n",
                     ch->get_name(), ipaddr_ntoa(&dest_ip_), dest_port_,
                     ch->get_sample_rate_hz(), ch->get_batch_count());
        }
    } else {
        // Enable all registered channels with their individual native rates
        for (size_t i = 0; i < channel_count_; ++i) {
            if (channels_[i].channel != nullptr) {
                channels_[i].channel->set_enabled(true);
                if (sampleRateHz > 0) {
                    channels_[i].channel->set_sample_rate_hz(sampleRateHz);
                }
                if (batchCount > 0 && sampleRateHz > 100) {
                    channels_[i].channel->set_batch_count(batchCount);
                }
            }
        }
        LOG_INFO("TelemetryService: all streams started to %s:%u\r\n",
                 ipaddr_ntoa(&dest_ip_), dest_port_);
    }

    is_streaming_ = true;
}

void TelemetryService::stop_streaming(uint32_t streamTag) {
    if (streamTag != 0) {
        ITelemetryChannel* ch = find_channel(streamTag);
        if (ch != nullptr) {
            ch->set_enabled(false);
            LOG_INFO("TelemetryService: stream '%s' stopped\r\n", ch->get_name());
        }
        bool any_enabled = false;
        for (size_t i = 0; i < channel_count_; ++i) {
            if (channels_[i].channel != nullptr && channels_[i].channel->is_enabled()) {
                any_enabled = true;
                break;
            }
        }
        is_streaming_ = any_enabled;
    } else {
        for (size_t i = 0; i < channel_count_; ++i) {
            if (channels_[i].channel != nullptr) {
                channels_[i].channel->set_enabled(false);
            }
        }
        is_streaming_ = false;
        LOG_INFO("TelemetryService: all streams stopped (Total pkts=%lu, samples=%lu)\r\n",
                 packets_sent_, samples_sent_);
    }
}

void TelemetryService::run() {
    LOG_INFO("TelemetryService: waiting for network ready...\r\n");

    while (!net_.is_ready()) {
        vTaskDelay(pdMS_TO_TICKS(100));
    }

    LOG_INFO("TelemetryService: network ready, multi-stream engine active on port %u\r\n", dest_port_);

    struct netconn* conn = nullptr;
    while (conn == nullptr) {
        conn = netconn_new(NETCONN_UDP);
        if (conn == nullptr) {
            LOG_ERR("TelemetryService: netconn_new failed, retrying...\r\n");
            vTaskDelay(pdMS_TO_TICKS(500));
        }
    }

    TickType_t now = xTaskGetTickCount();
    for (size_t i = 0; i < channel_count_; ++i) {
        channels_[i].last_wake_tick = now;
    }

    while (true) {
        if (!is_streaming_ || !net_.is_ready() || channel_count_ == 0) {
            vTaskDelay(pdMS_TO_TICKS(50));
            now = xTaskGetTickCount();
            for (size_t i = 0; i < channel_count_; ++i) {
                channels_[i].last_wake_tick = now;
            }
            continue;
        }

        now = xTaskGetTickCount();

        for (size_t i = 0; i < channel_count_; ++i) {
            auto& slot = channels_[i];
            if (!slot.channel || !slot.channel->is_enabled()) {
                continue;
            }

            uint16_t rate = slot.channel->get_sample_rate_hz();
            uint16_t batch = slot.channel->get_batch_count();
            if (rate == 0) rate = 1;
            if (batch == 0) batch = 1;

            // Packet period in ms = (batch * 1000) / rate
            uint32_t interval_ms = (static_cast<uint32_t>(batch) * 1000) / rate;
            if (interval_ms < 1) interval_ms = 1;

            TickType_t interval_ticks = pdMS_TO_TICKS(interval_ms);
            if (interval_ticks < 1) interval_ticks = 1;

            if ((now - slot.last_wake_tick) >= interval_ticks) {
                sendChannelFrame(conn, *slot.channel);
                slot.last_wake_tick = now;
            }
        }

        // Sleep for a 1ms scheduler tick
        vTaskDelay(pdMS_TO_TICKS(1));
    }
}

void TelemetryService::sendChannelFrame(struct netconn* conn, ITelemetryChannel& channel) {
    constexpr size_t MAX_TX_BUFFER = 1472; // Maximum single Ethernet UDP datagram
    static uint8_t tx_frame_buf[MAX_TX_BUFFER];

    uint16_t rate_hz = channel.get_sample_rate_hz();
    uint16_t ch_count = channel.get_channel_count();
    proto::SampleType s_type = channel.get_sample_type();
    size_t bytes_per_sample = channel.get_bytes_per_sample();

    if (bytes_per_sample == 0) return;

    uint16_t batch = channel.get_batch_count();
    uint16_t samples_to_send = (batch <= 100 && batch > 0) ? batch : 1;
    size_t max_allowed = (MAX_TX_BUFFER - sizeof(proto::PE_Header) - sizeof(proto::StreamPayloadHeader)) / bytes_per_sample;
    if (samples_to_send > max_allowed) {
        samples_to_send = static_cast<uint16_t>(max_allowed);
    }

    auto* hdr = reinterpret_cast<proto::PE_Header*>(tx_frame_buf);
    auto* stream_hdr = reinterpret_cast<proto::StreamPayloadHeader*>(
        tx_frame_buf + sizeof(proto::PE_Header)
    );
    void* sample_buffer = tx_frame_buf + sizeof(proto::PE_Header) + sizeof(proto::StreamPayloadHeader);

    // Generate samples via channel
    size_t actual_samples = channel.produce_samples(sample_buffer, samples_to_send);
    if (actual_samples == 0) {
        return;
    }

    uint16_t raw_data_bytes = static_cast<uint16_t>(actual_samples * bytes_per_sample);
    uint16_t total_payload_len = sizeof(proto::StreamPayloadHeader) + raw_data_bytes;

    // Populate stream metadata
    stream_hdr->timestamp_us    = static_cast<uint64_t>(xTaskGetTickCount()) * 1000;
    stream_hdr->stream_tag      = channel.get_tag();
    stream_hdr->sample_rate_hz  = rate_hz;
    stream_hdr->sample_count    = static_cast<uint16_t>(actual_samples);
    stream_hdr->channel_count   = ch_count;
    stream_hdr->sample_type     = static_cast<uint16_t>(s_type);

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
        samples_sent_ += actual_samples;

        LOG_DBG("TelemetryService: Tx stream=%s seq=%lu pkts=%lu to %s:%u (Rate=%uHz, Samples=%u)\r\n",
                channel.get_name(), seq_num_, packets_sent_, ipaddr_ntoa(&dest_ip_), dest_port_, rate_hz, actual_samples);
    }
}

} // namespace net::services
