#pragma once

#include "FreeRtosThread.h"
#include "NetManager.h"
#include "DiscoveryService.h"
#include "TelemetryService.h"
#include "OtaService.h"
#include "proto/ProtocolTypes.h"
#include "proto/PacketHelper.h"
#include "lwip/api.h"
#include "lwip/netbuf.h"

namespace net::services {

/**
 * @brief TCP Command server for remote parameter control, RPC, stream toggling, and OTA updates.
 */
class CommandService : public osal::Runnable {
public:
    CommandService(NetManager& netManager,
                   DiscoveryService& discoveryService,
                   TelemetryService& telemetryService,
                   uint16_t nodeId,
                   OtaService* otaService = nullptr);
    ~CommandService() = default;

    void set_ota_service(OtaService* otaService) { ota_ = otaService; }

    void run() override;

private:
    NetManager&       net_;
    DiscoveryService& discovery_;
    TelemetryService& telemetry_;
    OtaService*       ota_;
    uint16_t          node_id_;
    uint32_t          seq_num_;

    void handleClient(struct netconn* clientConn);
    void processCommand(struct netconn* clientConn,
                        const proto::PE_Header& hdr,
                        const uint8_t* payload,
                        const ip_addr_t* clientIp);
    void sendAckNack(struct netconn* clientConn,
                     uint16_t cmdId,
                     proto::StatusCode status,
                     uint32_t resultCode = 0);
};

} // namespace net::services
