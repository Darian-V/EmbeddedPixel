#include "CommandService.h"
#include "net_log.h"

// lwIP
#include "lwip/api.h"

// FreeRTOS
#include "FreeRTOS.h"
#include "task.h"

#if defined(STM32H7RSxx) || defined(STM32H7RS7XX) || defined(STM32H7S3XX) || defined(STM32H7S7XX)
#include "stm32h7rsxx_hal.h"
#elif defined(STM32H743xx)
#include "stm32h7xx_hal.h"
#endif

static inline void systemReset() {
#if defined(SCB) && defined(SCB_AIRCR_SYSRESETREQ_Msk)
    __DSB();
    SCB->AIRCR = ((0x5FAUL << SCB_AIRCR_VECTKEY_Pos) |
                  (SCB->AIRCR & SCB_AIRCR_PRIGROUP_Msk) |
                   SCB_AIRCR_SYSRESETREQ_Msk);
    __DSB();
    while (1) { __NOP(); }
#endif
}

namespace net::services {

CommandService::CommandService(NetManager& netManager,
                               DiscoveryService& discoveryService,
                               TelemetryService& telemetryService,
                               uint16_t nodeId)
    : net_(netManager),
      discovery_(discoveryService),
      telemetry_(telemetryService),
      node_id_(nodeId),
      seq_num_(0) {}

void CommandService::run() {
    LOG_INFO("CommandService: waiting for network ready...\r\n");

    while (!net_.is_ready()) {
        vTaskDelay(pdMS_TO_TICKS(100));
    }

    LOG_INFO("CommandService: starting TCP server on port %u\r\n", proto::PORT_COMMAND);

    struct netconn* server_conn = netconn_new(NETCONN_TCP);
    if (server_conn == nullptr) {
        LOG_ERR("CommandService: netconn_new TCP failed\r\n");
        vTaskDelete(nullptr);
        return;
    }

    err_t err = netconn_bind(server_conn, IP_ADDR_ANY, proto::PORT_COMMAND);
    if (err != ERR_OK) {
        LOG_ERR("CommandService: TCP bind failed err=%d\r\n", (int)err);
        netconn_delete(server_conn);
        vTaskDelete(nullptr);
        return;
    }

    err = netconn_listen(server_conn);
    if (err != ERR_OK) {
        LOG_ERR("CommandService: TCP listen failed err=%d\r\n", (int)err);
        netconn_delete(server_conn);
        vTaskDelete(nullptr);
        return;
    }

    while (true) {
        struct netconn* client_conn = nullptr;
        err = netconn_accept(server_conn, &client_conn);

        if (err == ERR_OK && client_conn != nullptr) {
            ip_addr_t client_ip;
            u16_t client_port;
            netconn_peer(client_conn, &client_ip, &client_port);
            LOG_INFO("CommandService: client connected from %s:%u\r\n",
                     ipaddr_ntoa(&client_ip), client_port);

            handleClient(client_conn);

            netconn_close(client_conn);
            netconn_delete(client_conn);
            LOG_INFO("CommandService: client disconnected\r\n");
        } else {
            vTaskDelay(pdMS_TO_TICKS(100));
        }
    }
}

void CommandService::handleClient(struct netconn* clientConn) {
    ip_addr_t client_ip;
    u16_t client_port;
    netconn_peer(clientConn, &client_ip, &client_port);

    while (true) {
        struct netbuf* rx_buf = nullptr;
        err_t err = netconn_recv(clientConn, &rx_buf);

        if (err != ERR_OK || rx_buf == nullptr) {
            break; // Connection closed or errored
        }

        void* data = nullptr;
        uint16_t len = 0;
        netbuf_data(rx_buf, &data, &len);

        if (len >= sizeof(proto::PE_Header)) {
            const auto* hdr = static_cast<const proto::PE_Header*>(data);
            if (proto::PacketHelper::ValidateHeader(*hdr, len) == proto::StatusCode::OK) {
                const uint8_t* payload = static_cast<const uint8_t*>(data) + sizeof(proto::PE_Header);
                processCommand(clientConn, *hdr, payload, &client_ip);
            } else {
                sendAckNack(clientConn, hdr->msg_type, proto::StatusCode::ERR_INVALID_MAGIC);
            }
        }

        netbuf_delete(rx_buf);
    }
}

void CommandService::processCommand(struct netconn* clientConn,
                                    const proto::PE_Header& hdr,
                                    const uint8_t* payload,
                                    const ip_addr_t* clientIp) {
    auto msg_type = static_cast<proto::MessageType>(hdr.msg_type);

    switch (msg_type) {
        case proto::MessageType::CMD_GET_NODE_INFO: {
            uint8_t tx_buf[sizeof(proto::PE_Header) + sizeof(proto::PayloadDiscoveryPong)];
            auto* resp_hdr = reinterpret_cast<proto::PE_Header*>(tx_buf);
            auto* pong = reinterpret_cast<proto::PayloadDiscoveryPong*>(tx_buf + sizeof(proto::PE_Header));

            pong->challenge_id = 0;
            pong->node_id      = node_id_;
            pong->node_state   = static_cast<uint16_t>(discovery_.get_state());
            pong->ip_addr      = net_.get_ip_addr();

            const uint8_t* mac = net_.get_mac_addr();
            if (mac != nullptr) {
                memcpy(pong->mac_addr, mac, 6);
            } else {
                memset(pong->mac_addr, 0, 6);
            }

            pong->fw_version   = 0x00010000;
            pong->uptime_ms    = xTaskGetTickCount() * portTICK_PERIOD_MS;
#if defined(HAL_GetUIDw0) || defined(STM32H7RSxx) || defined(STM32H7RS7XX) || defined(STM32H7S3XX) || defined(STM32H7S7XX) || defined(STM32H743xx)
            pong->hw_uid[0]    = HAL_GetUIDw0();
            pong->hw_uid[1]    = HAL_GetUIDw1();
            pong->hw_uid[2]    = HAL_GetUIDw2();
#else
            pong->hw_uid[0]    = 0;
            pong->hw_uid[1]    = 0;
            pong->hw_uid[2]    = 0;
#endif

            proto::PacketHelper::PopulateHeader(
                *resp_hdr,
                node_id_,
                proto::MessageType::CMD_GET_NODE_INFO_RESP,
                ++seq_num_,
                sizeof(proto::PayloadDiscoveryPong),
                proto::FLAG_IS_RESPONSE,
                false
            );

            netconn_write(clientConn, tx_buf, sizeof(tx_buf), NETCONN_COPY);
            break;
        }

        case proto::MessageType::CMD_START_STREAM: {
            uint32_t stream_tag = 0;
            uint16_t sample_rate = 0; // 0 = keep channel native rate
            uint16_t batch_count = 0; // 0 = keep channel native batch

            if (hdr.payload_len >= sizeof(proto::PayloadCommand)) {
                const auto* cmd = reinterpret_cast<const proto::PayloadCommand*>(payload);
                if (cmd->param2 > 100) {
                    stream_tag = cmd->param2; // FourCC tag passed in param2
                }

                if (cmd->cmd_id != 0) {
                    stream_tag = cmd->cmd_id;
                }

                // If explicit custom rate is requested (do not override if 10000 generic default on a low-frequency channel)
                if (cmd->param1 > 0 && cmd->param1 != 10000) {
                    sample_rate = cmd->param1;
                }

                // Only override batch count if specific stream is targeted
                if (stream_tag != 0 && cmd->param2 > 0 && cmd->param2 <= 100) {
                    batch_count = static_cast<uint16_t>(cmd->param2);
                }
            }

            ip_addr_t broadcast_ip;
            ip_addr_set_ip4_u32(&broadcast_ip, IPADDR_BROADCAST);
            telemetry_.start_streaming(broadcast_ip, proto::PORT_STREAM, stream_tag, sample_rate, batch_count);
            discovery_.set_state(proto::NodeState::STREAMING);
            sendAckNack(clientConn, hdr.msg_type, proto::StatusCode::OK, sample_rate);
            break;
        }

        case proto::MessageType::CMD_STOP_STREAM: {
            uint32_t stream_tag = 0;
            if (hdr.payload_len >= sizeof(proto::PayloadCommand)) {
                const auto* cmd = reinterpret_cast<const proto::PayloadCommand*>(payload);
                stream_tag = (cmd->param2 > 100) ? cmd->param2 : cmd->cmd_id;
            }
            telemetry_.stop_streaming(stream_tag);
            if (!telemetry_.is_streaming()) {
                discovery_.set_state(proto::NodeState::IDLE);
            }
            sendAckNack(clientConn, hdr.msg_type, proto::StatusCode::OK);
            break;
        }

        case proto::MessageType::CMD_GET_STREAMS: {
            size_t count = telemetry_.get_channel_count();
            uint16_t total_payload_len = static_cast<uint16_t>(
                sizeof(proto::PayloadGetStreamsResp) + (count * sizeof(proto::StreamDescriptor))
            );

            // Response buffer (up to MAX_CHANNELS descriptors)
            uint8_t tx_buf[sizeof(proto::PE_Header) +
                           sizeof(proto::PayloadGetStreamsResp) +
                           (net::services::TelemetryService::MAX_CHANNELS * sizeof(proto::StreamDescriptor))];

            auto* resp_hdr = reinterpret_cast<proto::PE_Header*>(tx_buf);
            auto* resp_body = reinterpret_cast<proto::PayloadGetStreamsResp*>(
                tx_buf + sizeof(proto::PE_Header)
            );
            auto* desc_array = reinterpret_cast<proto::StreamDescriptor*>(
                tx_buf + sizeof(proto::PE_Header) + sizeof(proto::PayloadGetStreamsResp)
            );

            resp_body->stream_count = static_cast<uint16_t>(count);
            resp_body->reserved     = 0;

            for (size_t i = 0; i < count; ++i) {
                ITelemetryChannel* ch = telemetry_.get_channel_at(i);
                if (ch != nullptr) {
                    desc_array[i].stream_tag      = ch->get_tag();
                    memset(desc_array[i].name, 0, sizeof(desc_array[i].name));
                    strncpy(desc_array[i].name, ch->get_name(), sizeof(desc_array[i].name) - 1);
                    desc_array[i].sample_rate_hz  = ch->get_sample_rate_hz();
                    desc_array[i].batch_count     = ch->get_batch_count();
                    desc_array[i].channel_count   = ch->get_channel_count();
                    desc_array[i].sample_type     = static_cast<uint16_t>(ch->get_sample_type());
                    desc_array[i].is_enabled      = ch->is_enabled() ? 1 : 0;
                    memset(desc_array[i].reserved, 0, sizeof(desc_array[i].reserved));
                }
            }

            proto::PacketHelper::PopulateHeader(
                *resp_hdr,
                node_id_,
                proto::MessageType::CMD_GET_STREAMS_RESP,
                ++seq_num_,
                total_payload_len,
                proto::FLAG_IS_RESPONSE,
                false
            );

            uint16_t total_frame_len = sizeof(proto::PE_Header) + total_payload_len;
            netconn_write(clientConn, tx_buf, total_frame_len, NETCONN_COPY);
            LOG_INFO("CommandService: sent stream list (%u streams)\r\n", static_cast<unsigned>(count));
            break;
        }

        case proto::MessageType::CMD_REBOOT: {
            sendAckNack(clientConn, hdr.msg_type, proto::StatusCode::OK);
            vTaskDelay(pdMS_TO_TICKS(200));
            systemReset();
            break;
        }

        default: {
            LOG_ERR("CommandService: unknown command 0x%04X\r\n", hdr.msg_type);
            sendAckNack(clientConn, hdr.msg_type, proto::StatusCode::ERR_UNKNOWN_CMD);
            break;
        }
    }
}

void CommandService::sendAckNack(struct netconn* clientConn,
                                 uint16_t cmdId,
                                 proto::StatusCode status,
                                 uint32_t resultCode) {
    uint8_t tx_buf[sizeof(proto::PE_Header) + sizeof(proto::PayloadAckNack)];
    auto* hdr = reinterpret_cast<proto::PE_Header*>(tx_buf);
    auto* ack = reinterpret_cast<proto::PayloadAckNack*>(tx_buf + sizeof(proto::PE_Header));

    ack->cmd_id      = cmdId;
    ack->status_code = static_cast<uint16_t>(status);
    ack->result_data = resultCode;
    ack->reserved    = 0;

    proto::MessageType resp_type = (status == proto::StatusCode::OK)
                                   ? proto::MessageType::CMD_ACK
                                   : proto::MessageType::CMD_NACK;

    uint8_t flags = proto::FLAG_IS_RESPONSE;
    if (status != proto::StatusCode::OK) {
        flags |= proto::FLAG_ERROR;
    }

    proto::PacketHelper::PopulateHeader(
        *hdr,
        node_id_,
        resp_type,
        ++seq_num_,
        sizeof(proto::PayloadAckNack),
        flags,
        false
    );

    netconn_write(clientConn, tx_buf, sizeof(tx_buf), NETCONN_COPY);
}

} // namespace net::services
