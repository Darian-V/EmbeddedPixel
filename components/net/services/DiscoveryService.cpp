#include "DiscoveryService.h"
#include "net_log.h"

// lwIP
#include "lwip/api.h"
#include "lwip/ip_addr.h"

// FreeRTOS
#include "FreeRTOS.h"
#include "task.h"

#if defined(STM32H7RSxx) || defined(STM32H7RS7XX) || defined(STM32H7S3XX) || defined(STM32H7S7XX)
#include "stm32h7rsxx_hal.h"
#elif defined(STM32H743xx)
#include "stm32h7xx_hal.h"
#endif

namespace net::services {

DiscoveryService::DiscoveryService(NetManager& netManager, uint16_t nodeId, hal::ITempSensor* tempSensor)
    : net_(netManager),
      node_id_(nodeId),
      temp_sensor_(tempSensor),
      seq_num_(0),
      state_(proto::NodeState::STREAMING) {
    if (temp_sensor_ != nullptr) {
        temp_sensor_->init();
    }
}

void DiscoveryService::run() {
    LOG_INFO("DiscoveryService: waiting for network ready...\r\n");

    // Wait until network link is up and IP assigned
    while (!net_.is_ready()) {
        vTaskDelay(pdMS_TO_TICKS(100));
    }

    LOG_INFO("DiscoveryService: starting UDP listener on port %u\r\n", proto::PORT_DISCOVERY);

    struct netconn* conn = netconn_new(NETCONN_UDP);
    if (conn == nullptr) {
        LOG_ERR("DiscoveryService: failed to create UDP netconn\r\n");
        vTaskDelete(nullptr);
        return;
    }

    err_t err = netconn_bind(conn, IP_ADDR_ANY, proto::PORT_DISCOVERY);
    if (err != ERR_OK) {
        LOG_ERR("DiscoveryService: bind failed err=%d\r\n", (int)err);
        netconn_delete(conn);
        vTaskDelete(nullptr);
        return;
    }

    // Set receive timeout so loop can periodically send heartbeats
    netconn_set_recvtimeout(conn, 250); // 250 ms timeout

    TickType_t last_hb_tick = xTaskGetTickCount();

    while (true) {
        if (!net_.is_ready()) {
            vTaskDelay(pdMS_TO_TICKS(200));
            continue;
        }

        // ── 1. Periodic Heartbeat Transmission (1 Hz) ──────────────────────
        TickType_t now = xTaskGetTickCount();
        if ((now - last_hb_tick) >= pdMS_TO_TICKS(1000)) {
            sendHeartbeat(conn);
            last_hb_tick = now;
        }

        // ── 2. Receive and process discovery packets ───────────────────────
        struct netbuf* rx_buf = nullptr;
        err = netconn_recv(conn, &rx_buf);

        if (err == ERR_OK && rx_buf != nullptr) {
            handleIncomingPacket(conn, rx_buf);
            netbuf_delete(rx_buf);
        } else if (err != ERR_TIMEOUT && err != ERR_OK) {
            LOG_DBG("DiscoveryService: recv err=%d\r\n", (int)err);
        }
    }
}

void DiscoveryService::sendHeartbeat(struct netconn* conn) {
    uint8_t tx_buffer[sizeof(proto::PE_Header) + sizeof(proto::PayloadHeartbeat)];
    auto* hdr = reinterpret_cast<proto::PE_Header*>(tx_buffer);
    auto* payload = reinterpret_cast<proto::PayloadHeartbeat*>(tx_buffer + sizeof(proto::PE_Header));

    payload->uptime_ms       = xTaskGetTickCount() * portTICK_PERIOD_MS;
    payload->fw_version      = 0x00010000; // v1.0.0
    payload->node_state      = static_cast<uint8_t>(state_);
    payload->active_streams  = (state_ == proto::NodeState::STREAMING) ? 0x01 : 0x00;
    payload->vdd_mv          = 3300;

    int16_t temp_c_x10 = 250;
    if (temp_sensor_ != nullptr) {
        int32_t current_temp = 0;
        if (temp_sensor_->get_temperature(current_temp)) {
            temp_c_x10 = static_cast<int16_t>(current_temp * 10);
        }
    }
    payload->core_temp_c_x10 = temp_c_x10;
    payload->reserved        = 0;

    proto::PacketHelper::PopulateHeader(
        *hdr,
        node_id_,
        proto::MessageType::HEARTBEAT,
        ++seq_num_,
        sizeof(proto::PayloadHeartbeat),
        0,
        false
    );

    struct netbuf* buf = netbuf_new();
    if (buf == nullptr) {
        return;
    }

    netbuf_ref(buf, tx_buffer, sizeof(tx_buffer));

    ip_addr_t broadcast_addr;
    ip_addr_set_ip4_u32(&broadcast_addr, IPADDR_BROADCAST);

    netconn_sendto(conn, buf, &broadcast_addr, proto::PORT_DISCOVERY);
    netbuf_delete(buf);

    LOG_DBG("DiscoveryService: Tx Heartbeat seq=%lu state=%u\r\n", seq_num_, static_cast<uint8_t>(state_));
}

void DiscoveryService::handleIncomingPacket(struct netconn* conn, struct netbuf* rxBuf) {
    void* data = nullptr;
    uint16_t len = 0;
    netbuf_data(rxBuf, &data, &len);

    if (len < sizeof(proto::PE_Header)) {
        return;
    }

    const auto* hdr = static_cast<const proto::PE_Header*>(data);
    if (proto::PacketHelper::ValidateHeader(*hdr, len) != proto::StatusCode::OK) {
        return;
    }

    if (hdr->msg_type == static_cast<uint16_t>(proto::MessageType::DISCOVERY_PING)) {
        if (hdr->payload_len < sizeof(proto::PayloadDiscoveryPing)) {
            return;
        }

        const auto* ping = reinterpret_cast<const proto::PayloadDiscoveryPing*>(
            static_cast<const uint8_t*>(data) + sizeof(proto::PE_Header)
        );

        // Check if targeted to all nodes (0) or specifically to us
        if (ping->target_node_id != 0 && ping->target_node_id != node_id_) {
            return;
        }

        // Construct unicast DISCOVERY_PONG response
        uint8_t tx_buffer[sizeof(proto::PE_Header) + sizeof(proto::PayloadDiscoveryPong)];
        auto* resp_hdr = reinterpret_cast<proto::PE_Header*>(tx_buffer);
        auto* pong = reinterpret_cast<proto::PayloadDiscoveryPong*>(tx_buffer + sizeof(proto::PE_Header));

        pong->challenge_id = ping->challenge_id;
        pong->node_id      = node_id_;
        pong->node_state   = static_cast<uint16_t>(state_);
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
        pong->hw_uid[0] = HAL_GetUIDw0();
        pong->hw_uid[1] = HAL_GetUIDw1();
        pong->hw_uid[2] = HAL_GetUIDw2();
#else
        pong->hw_uid[0] = 0;
        pong->hw_uid[1] = 0;
        pong->hw_uid[2] = 0;
#endif

        proto::PacketHelper::PopulateHeader(
            *resp_hdr,
            node_id_,
            proto::MessageType::DISCOVERY_PONG,
            ++seq_num_,
            sizeof(proto::PayloadDiscoveryPong),
            proto::FLAG_IS_RESPONSE,
            false
        );

        struct netbuf* resp_buf = netbuf_new();
        if (resp_buf != nullptr) {
            netbuf_ref(resp_buf, tx_buffer, sizeof(tx_buffer));
            const ip_addr_t* from_ip = netbuf_fromaddr(rxBuf);
            uint16_t from_port       = netbuf_fromport(rxBuf);
            netconn_sendto(conn, resp_buf, from_ip, from_port);
            netbuf_delete(resp_buf);
            LOG_INFO("DiscoveryService: sent PONG to %s:%u\r\n", ipaddr_ntoa(from_ip), from_port);
        }
    }
}

} // namespace net::services
