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
    DiscoveryService(NetManager& netManager, uint16_t nodeId, hal::ITempSensor* tempSensor = nullptr);
    ~DiscoveryService() = default;

    void run() override;

    void set_state(proto::NodeState state) { state_ = state; }
    proto::NodeState get_state() const { return state_; }

    uint16_t get_node_id() const { return node_id_; }
    void set_node_id(uint16_t id) { node_id_ = id; }

private:
    NetManager&        net_;
    uint16_t           node_id_;
    hal::ITempSensor*  temp_sensor_;
    uint32_t           seq_num_;
    proto::NodeState   state_;

    void sendHeartbeat(struct netconn* conn);
    void handleIncomingPacket(struct netconn* conn, struct netbuf* rxBuf);
};

} // namespace net::services
