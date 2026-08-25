#include "CommandService.h"
#include "SystemController.h"
#include "CliEngine.h"
#include "net_log.h"

// lwIP
#include "lwip/api.h"
#include "lwip/ip.h"
#include "lwip/tcp.h"

// FreeRTOS
#include "FreeRTOS.h"
#include "task.h"

#include "stm32h7rsxx_hal.h"

static inline void systemReset() {
    __DSB();
    NVIC_SystemReset();
    while (1) { __NOP(); }
}

namespace net::services {

CommandService::CommandService(NetManager& netManager,
                               DiscoveryService& discoveryService,
                               TelemetryService& telemetryService,
                               uint16_t nodeId,
                               OtaService* otaService,
                               sys::SystemController* sysCtrl,
                               sys::CliEngine* cli)
    : net_(netManager),
      discovery_(discoveryService),
      telemetry_(telemetryService),
      ota_(otaService),
      sys_ctrl_(sysCtrl),
      cli_(cli),
      node_id_(nodeId),
      seq_num_(0) {}

void CommandService::run() {
    LOG_INFO("CommandService: waiting for network ready...\r\n");

    while (!net_.is_ready()) {
        vTaskDelay(pdMS_TO_TICKS(100));
    }

    LOG_INFO("CommandService: starting TCP server on port %u\r\n", proto::PORT_COMMAND);

    struct netconn* server_conn = nullptr;

    while (true) {
        if (server_conn == nullptr) {
            server_conn = netconn_new(NETCONN_TCP);
            if (server_conn == nullptr) {
                LOG_ERR("CommandService: netconn_new TCP failed\r\n");
                vTaskDelay(pdMS_TO_TICKS(500));
                continue;
            }

            if (server_conn->pcb.tcp != nullptr) {
                ip_set_option(server_conn->pcb.tcp, SOF_REUSEADDR);
                tcp_nagle_disable(server_conn->pcb.tcp);
            }

            err_t err = netconn_bind(server_conn, IP_ADDR_ANY, proto::PORT_COMMAND);
            if (err != ERR_OK) {
                LOG_ERR("CommandService: TCP bind failed err=%d\r\n", (int)err);
                netconn_delete(server_conn);
                server_conn = nullptr;
                vTaskDelay(pdMS_TO_TICKS(500));
                continue;
            }

            err = netconn_listen(server_conn);
            if (err != ERR_OK) {
                LOG_ERR("CommandService: TCP listen failed err=%d\r\n", (int)err);
                netconn_delete(server_conn);
                server_conn = nullptr;
                vTaskDelay(pdMS_TO_TICKS(500));
                continue;
            }
            LOG_INFO("CommandService: TCP server listening on port %u\r\n", proto::PORT_COMMAND);
        }

        struct netconn* client_conn = nullptr;
        err_t err = netconn_accept(server_conn, &client_conn);

        if (err == ERR_OK && client_conn != nullptr) {
            if (client_conn->pcb.tcp != nullptr) {
                tcp_nagle_disable(client_conn->pcb.tcp);
            }
            netconn_set_recvtimeout(client_conn, 2000);
            netconn_set_sendtimeout(client_conn, 0);
            ip_addr_t client_ip;
            u16_t client_port;
            netconn_peer(client_conn, &client_ip, &client_port);
            handleClient(client_conn);

            // Graceful shutdown: close connection to flush all TX data & FIN before deleting netconn
            err_t close_err = netconn_close(client_conn);
            LOG_INFO("CommandService: netconn_close err=%d\r\n", (int)close_err);
            vTaskDelay(pdMS_TO_TICKS(50));
            err_t del_err = netconn_delete(client_conn);
            LOG_INFO("CommandService: client disconnected del_err=%d\r\n", (int)del_err);
        } else {
            if (err != ERR_TIMEOUT) {
                LOG_ERR("CommandService: netconn_accept err=%d, re-arming listen socket\r\n", (int)err);
                netconn_close(server_conn);
                netconn_delete(server_conn);
                server_conn = nullptr;
                vTaskDelay(pdMS_TO_TICKS(100));
            } else {
                vTaskDelay(pdMS_TO_TICKS(10));
            }
        }
    }
}

namespace {

class NetconnStreamReader {
public:
    explicit NetconnStreamReader(struct netconn* conn)
        : conn_(conn), cur_buf_(nullptr), buf_offset_(0), buf_len_(0) {}

    ~NetconnStreamReader() {
        if (cur_buf_ != nullptr) {
            netbuf_delete(cur_buf_);
        }
    }

    bool readExact(uint8_t* dest, size_t needed) {
        size_t copied = 0;
        while (copied < needed) {
            if (cur_buf_ == nullptr || buf_offset_ >= buf_len_) {
                if (cur_buf_ != nullptr) {
                    netbuf_delete(cur_buf_);
                    cur_buf_ = nullptr;
                }
                err_t err = netconn_recv(conn_, &cur_buf_);
                if (err != ERR_OK || cur_buf_ == nullptr) {
                    return false;
                }
                buf_len_ = netbuf_len(cur_buf_);
                buf_offset_ = 0;
            }

            size_t available = buf_len_ - buf_offset_;
            size_t to_copy = (needed - copied < available) ? (needed - copied) : available;
            netbuf_copy_partial(cur_buf_, dest + copied, to_copy, buf_offset_);
            buf_offset_ += to_copy;
            copied += to_copy;
        }
        return true;
    }

private:
    struct netconn* conn_;
    struct netbuf*  cur_buf_;
    size_t          buf_offset_;
    size_t          buf_len_;
};

static inline err_t writeClientData(struct netconn* conn, const void* data, size_t size) {
    size_t written = 0;
    return netconn_write_partly(conn, data, size, NETCONN_COPY, &written);
}

} // namespace

void CommandService::handleClient(struct netconn* clientConn) {
    ip_addr_t client_ip;
    u16_t client_port;
    netconn_peer(clientConn, &client_ip, &client_port);

    NetconnStreamReader reader(clientConn);
    proto::PE_Header hdr;
    static uint8_t s_payload_buffer[2048];

    while (true) {
        if (!reader.readExact(reinterpret_cast<uint8_t*>(&hdr), sizeof(hdr))) {
            LOG_INFO("CommandService: readExact header failed/timeout\r\n");
            break; // Connection closed or timed out
        }

        LOG_INFO("CommandService: RX hdr magic=0x%04X type=0x%04X len=%u\r\n",
                 (unsigned)hdr.magic, (unsigned)hdr.msg_type, (unsigned)hdr.payload_len);

        if (hdr.magic != proto::PE_MAGIC) {
            sendAckNack(clientConn, hdr.msg_type, proto::StatusCode::ERR_INVALID_MAGIC);
            break;
        }

        if (hdr.payload_len > sizeof(s_payload_buffer)) {
            sendAckNack(clientConn, hdr.msg_type, proto::StatusCode::ERR_INVALID_PAYLOAD);
            break;
        }

        if (hdr.payload_len > 0) {
            if (!reader.readExact(s_payload_buffer, hdr.payload_len)) {
                LOG_INFO("CommandService: readExact payload (%u bytes) failed\r\n", (unsigned)hdr.payload_len);
                break;
            }
        }

        processCommand(clientConn, hdr, s_payload_buffer, &client_ip);

        // For discrete RPC commands, exit immediately so accept() can service next client
        if (hdr.msg_type != static_cast<uint16_t>(proto::MessageType::CMD_OTA_DATA)) {
            break;
        }
        netconn_set_recvtimeout(clientConn, 1000);
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

            pong->challenge_id       = 0;
            pong->node_id            = node_id_;
            pong->node_state         = static_cast<uint16_t>(discovery_.get_state());
            pong->ip_addr            = net_.get_ip_addr();

            const uint8_t* mac = net_.get_mac_addr();
            if (mac != nullptr) {
                memcpy(pong->mac_addr, mac, 6);
            } else {
                memset(pong->mac_addr, 0, 6);
            }

            pong->board_id           = (sys_ctrl_ != nullptr) ? sys_ctrl_->get_board_id() : discovery_.get_board_id();
            pong->fw_version         = (sys_ctrl_ != nullptr) ? sys_ctrl_->get_app_version() : discovery_.get_fw_version();
            pong->uptime_ms          = xTaskGetTickCount() * portTICK_PERIOD_MS;
#if defined(HAL_GetUIDw0) || defined(STM32H7RSxx) || defined(STM32H7RS7XX) || defined(STM32H7S3XX) || defined(STM32H7S7XX) || defined(STM32H743xx)
            pong->hw_uid[0]          = HAL_GetUIDw0();
            pong->hw_uid[1]          = HAL_GetUIDw1();
            pong->hw_uid[2]          = HAL_GetUIDw2();
#else
            pong->hw_uid[0]          = 0;
            pong->hw_uid[1]          = 0;
            pong->hw_uid[2]          = 0;
#endif
            pong->bootloader_version = (sys_ctrl_ != nullptr) ? sys_ctrl_->get_bootloader_version() : discovery_.get_bootloader_version();
            pong->feature_flags      = (sys_ctrl_ != nullptr) ? sys_ctrl_->get_feature_flags() : discovery_.get_feature_flags();

            proto::PacketHelper::PopulateHeader(
                *resp_hdr,
                node_id_,
                proto::MessageType::CMD_GET_NODE_INFO_RESP,
                ++seq_num_,
                sizeof(proto::PayloadDiscoveryPong),
                proto::FLAG_IS_RESPONSE,
                false
            );

            writeClientData(clientConn, tx_buf, sizeof(tx_buf));
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
                } else if (cmd->cmd_id != 0) {
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
            writeClientData(clientConn, tx_buf, total_frame_len);
            LOG_INFO("CommandService: sent stream list (%u streams)\r\n", static_cast<unsigned>(count));
            break;
        }

        case proto::MessageType::CMD_OTA_BEGIN: {
            if (ota_ == nullptr) {
                sendAckNack(clientConn, hdr.msg_type, proto::StatusCode::ERR_INTERNAL);
                break;
            }
            if (hdr.payload_len < sizeof(proto::PayloadOtaBegin)) {
                sendAckNack(clientConn, hdr.msg_type, proto::StatusCode::ERR_INVALID_PAYLOAD);
                break;
            }
            const auto* req = reinterpret_cast<const proto::PayloadOtaBegin*>(payload);
            uint8_t tx_buf[sizeof(proto::PE_Header) + sizeof(proto::PayloadOtaBeginResp)];
            auto* resp_hdr = reinterpret_cast<proto::PE_Header*>(tx_buf);
            auto* resp_body = reinterpret_cast<proto::PayloadOtaBeginResp*>(tx_buf + sizeof(proto::PE_Header));

            proto::StatusCode status = ota_->handleBegin(*req, *resp_body);
            uint8_t flags = proto::FLAG_IS_RESPONSE;
            if (status != proto::StatusCode::OK) {
                flags |= proto::FLAG_ERROR;
            }

            proto::PacketHelper::PopulateHeader(
                *resp_hdr,
                node_id_,
                proto::MessageType::CMD_OTA_BEGIN_RESP,
                ++seq_num_,
                sizeof(proto::PayloadOtaBeginResp),
                flags,
                false
            );
            writeClientData(clientConn, tx_buf, sizeof(tx_buf));
            break;
        }

        case proto::MessageType::CMD_OTA_DATA: {
            if (ota_ == nullptr) {
                sendAckNack(clientConn, hdr.msg_type, proto::StatusCode::ERR_INTERNAL);
                break;
            }
            if (hdr.payload_len < sizeof(proto::PayloadOtaData)) {
                sendAckNack(clientConn, hdr.msg_type, proto::StatusCode::ERR_INVALID_PAYLOAD);
                break;
            }
            const auto* req = reinterpret_cast<const proto::PayloadOtaData*>(payload);
            const uint8_t* chunk_bytes = payload + sizeof(proto::PayloadOtaData);
            uint16_t chunk_len = req->chunk_len;

            proto::StatusCode status = ota_->handleData(*req, chunk_bytes, chunk_len);
            sendAckNack(clientConn, hdr.msg_type, status, req->offset + chunk_len);
            break;
        }

        case proto::MessageType::CMD_OTA_END: {
            if (ota_ == nullptr) {
                sendAckNack(clientConn, hdr.msg_type, proto::StatusCode::ERR_INTERNAL);
                break;
            }
            if (hdr.payload_len < sizeof(proto::PayloadOtaEnd)) {
                sendAckNack(clientConn, hdr.msg_type, proto::StatusCode::ERR_INVALID_PAYLOAD);
                break;
            }
            const auto* req = reinterpret_cast<const proto::PayloadOtaEnd*>(payload);
            proto::StatusCode status = ota_->handleEnd(*req);
            sendAckNack(clientConn, hdr.msg_type, status);

            if (ota_->is_reboot_pending()) {
                LOG_INFO("CommandService: OTA complete. Rebooting in 300ms...\r\n");
                vTaskDelay(pdMS_TO_TICKS(300));
                systemReset();
            }
            break;
        }

        case proto::MessageType::CMD_OTA_GET_STATUS: {
            if (ota_ == nullptr) {
                sendAckNack(clientConn, hdr.msg_type, proto::StatusCode::ERR_INTERNAL);
                break;
            }
            uint8_t tx_buf[sizeof(proto::PE_Header) + sizeof(proto::PayloadOtaStatusResp)];
            auto* resp_hdr = reinterpret_cast<proto::PE_Header*>(tx_buf);
            auto* resp_body = reinterpret_cast<proto::PayloadOtaStatusResp*>(tx_buf + sizeof(proto::PE_Header));

            ota_->getStatus(*resp_body);
            proto::PacketHelper::PopulateHeader(
                *resp_hdr,
                node_id_,
                proto::MessageType::CMD_OTA_GET_STATUS_RESP,
                ++seq_num_,
                sizeof(proto::PayloadOtaStatusResp),
                proto::FLAG_IS_RESPONSE,
                false
            );
            writeClientData(clientConn, tx_buf, sizeof(tx_buf));
            break;
        }

        case proto::MessageType::CMD_OTA_ABORT: {
            if (ota_ != nullptr) {
                ota_->handleAbort();
            }
            sendAckNack(clientConn, hdr.msg_type, proto::StatusCode::OK);
            break;
        }

        case proto::MessageType::CMD_CLI_EXEC: {
            if (cli_ == nullptr) {
                sendAckNack(clientConn, hdr.msg_type, proto::StatusCode::ERR_INTERNAL);
                break;
            }

            char cmd_str[128] = {0};
            if (hdr.payload_len >= sizeof(proto::PayloadCliExec)) {
                const auto* req = reinterpret_cast<const proto::PayloadCliExec*>(payload);
                const char* cmd_text = reinterpret_cast<const char*>(payload + sizeof(proto::PayloadCliExec));
                uint16_t text_len = req->cmd_len;
                if (text_len > sizeof(cmd_str) - 1) text_len = sizeof(cmd_str) - 1;
                memcpy(cmd_str, cmd_text, text_len);
                cmd_str[text_len] = '\0';
            } else {
                size_t copy_len = (hdr.payload_len < sizeof(cmd_str) - 1) ? hdr.payload_len : sizeof(cmd_str) - 1;
                if (copy_len > 0) {
                    memcpy(cmd_str, payload, copy_len);
                    cmd_str[copy_len] = '\0';
                }
            }

            char resp_str[512] = {0};
            int resp_len = cli_->execute(cmd_str, resp_str, sizeof(resp_str));
            LOG_INFO("CommandService: CLI exec cmd='%s' resp_len=%d\r\n", cmd_str, resp_len);

            uint8_t tx_buf[sizeof(proto::PE_Header) + sizeof(proto::PayloadCliExecResp) + sizeof(resp_str)];
            auto* resp_hdr = reinterpret_cast<proto::PE_Header*>(tx_buf);
            auto* resp_body = reinterpret_cast<proto::PayloadCliExecResp*>(tx_buf + sizeof(proto::PE_Header));
            char* resp_text = reinterpret_cast<char*>(tx_buf + sizeof(proto::PE_Header) + sizeof(proto::PayloadCliExecResp));

            resp_body->status_code = static_cast<uint16_t>(proto::StatusCode::OK);
            resp_body->resp_len    = static_cast<uint16_t>(resp_len);
            if (resp_len > 0) {
                memcpy(resp_text, resp_str, resp_len);
            }

            uint16_t total_payload_len = sizeof(proto::PayloadCliExecResp) + resp_len;
            proto::PacketHelper::PopulateHeader(
                *resp_hdr,
                node_id_,
                proto::MessageType::CMD_CLI_EXEC_RESP,
                ++seq_num_,
                total_payload_len,
                proto::FLAG_IS_RESPONSE,
                false
            );

            err_t w_err = writeClientData(clientConn, tx_buf, sizeof(proto::PE_Header) + total_payload_len);
            LOG_INFO("CommandService: write total_len=%u err=%d\r\n",
                     (unsigned)(sizeof(proto::PE_Header) + total_payload_len), (int)w_err);
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

    writeClientData(clientConn, tx_buf, sizeof(tx_buf));
}

} // namespace net::services
