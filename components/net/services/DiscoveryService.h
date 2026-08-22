#pragma once

#include "FreeRtosThread.h"
#include "NetManager.h"
#include "proto/ProtocolTypes.h"
#include "proto/PacketHelper.h"
#include "lwip/api.h"
#include "lwip/netbuf.h"

#include "ITempSensor.h"

namespace net::services {

/**
 * @brief Handles periodic 1 Hz UDP heartbeat broadcasting and responding
 *        to host DISCOVERY_PING probes.
 */
class DiscoveryService : public osal::Runnable {
public:
    DiscoveryService(NetManager& netManager, uint16_t nodeId, hal::ITempSensor* tempSensor = nullptr, uint32_t fwVersion = 0x00010000);
    ~DiscoveryService() = default;

    void run() override;

    void set_state(proto::NodeState state) { state_ = state; }
    proto::NodeState get_state() const { return state_; }

    uint16_t get_node_id() const { return node_id_; }
    void set_node_id(uint16_t id) { node_id_ = id; }

    uint32_t get_fw_version() const { return fw_version_; }
    void set_fw_version(uint32_t ver) { fw_version_ = ver; }

    uint32_t get_bootloader_version() const { return bootloader_version_; }
    void set_bootloader_version(uint32_t ver) { bootloader_version_ = ver; }

    uint16_t get_board_id() const { return board_id_; }
    void set_board_id(uint16_t id) { board_id_ = id; }

    uint32_t get_feature_flags() const { return feature_flags_; }
    void set_feature_flags(uint32_t flags) { feature_flags_ = flags; }

private:
    NetManager&        net_;
    uint16_t           node_id_;
    hal::ITempSensor*  temp_sensor_;
    uint32_t           fw_version_;
    uint32_t           bootloader_version_;
    uint16_t           board_id_;
    uint32_t           feature_flags_;
    uint32_t           seq_num_;
    proto::NodeState   state_;

    void sendHeartbeat(struct netconn* conn);
    void handleIncomingPacket(struct netconn* conn, struct netbuf* rxBuf);
};

} // namespace net::services
