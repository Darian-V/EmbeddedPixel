# Ethernet Node-to-Host Communication Architecture Specification

**Version:** 1.0.0  
**Status:** Approved / Base Design  
**Target Hardware:** STM32H7 Series (e.g. Nucleo-H7S3L8, PixelJam, Custom PCBAs)  
**Host Platform:** Linux Workstation / Embedded Industrial Computer (x86_64 / ARM64)  
**Stack Components:** FreeRTOS (OSAL), lwIP (TCP/IP), STM32 Ethernet MAC + LAN8742 RMII PHY  

---

## Table of Contents
1. [Executive Summary & Goals](#1-executive-summary--goals)
2. [System Topology & Physical Layer](#2-system-topology--physical-layer)
3. [Transport Layer & Port Mapping](#3-transport-layer--port-mapping)
4. [Binary Wire Protocol Specification](#4-binary-wire-protocol-specification)
5. [Streaming Performance & Multi-Node Scaling Analysis](#5-streaming-performance--multi-node-scaling-analysis)
6. [Embedded Node Software Architecture](#6-embedded-node-software-architecture)
7. [Host Ingestion Architecture Guidelines](#7-host-ingestion-architecture-guidelines)
8. [Reliability, Recovery & Edge Cases](#8-reliability-recovery--edge-cases)
9. [Future Extensions (OTA & Precision Time Protocol)](#9-future-extensions-ota--precision-time-protocol)

---

## 1. Executive Summary & Goals

This document specifies the network communication architecture for distributed STM32 embedded nodes (PCBAs) transmitting high-speed telemetry and receiving control commands from a central Linux host computer over an Ethernet network.

### Core Objectives
- **High-Throughput / High-Frequency Streaming**: Support data acquisition at sampling frequencies up to **10 kHz** per node with microsecond-level timing jitter.
- **Scalable Multi-Node Topology**: Support dozens to hundreds of concurrent PCBA nodes on a standard switched Gigabit Ethernet network without packet loss or host saturation.
- **Low MCU Overhead**: Maintain STM32 Cortex-M7 CPU utilization below **5%** for network I/O through zero-copy DMA buffers and batching strategies.
- **Zero-Configuration Discovery**: Automatic node discovery and registration via UDP heartbeat/probe mechanisms.
- **Robust Control & Configuration**: Reliable TCP-based Remote Procedure Call (RPC) interface for parameter updates, calibration, and state management.

---

## 2. System Topology & Physical Layer

### 2.1 Network Topology

The system uses a standard Star / Tree Ethernet topology centered around an Ethernet switch.

```
                            +------------------------------------+
                            |         Linux Host Computer        |
                            |   - Ingestion Engine (1 Gbps)      |
                            |   - Node Registry & Monitoring     |
                            |   - Control / Calibration Server   |
                            +-----------------+------------------+
                                              |
                                     1 Gbps Ethernet Link
                                              |
                            +-----------------v------------------+
                            |     Gigabit Ethernet Switch        |
                            |       (Layer 2 Non-Blocking)       |
                            +----+----------+----------+---------+
                                 |          |          |
                      100M Links |          |          | 100M Links
                                 |          |          |
             +-------------------+          |          +-------------------+
             |                              |                              |
+------------v------------+    +------------v------------+    +------------v------------+
|  PCBA Node 1 (STM32H7)  |    |  PCBA Node 2 (STM32H7)  |    |  PCBA Node N (STM32H7)  |
|  - LAN8742 100BASE-TX   |    |  - LAN8742 100BASE-TX   |    |  - LAN8742 100BASE-TX   |
|  - Node ID: 0x0001      |    |  - Node ID: 0x0002      |    |  - Node ID: 0x000N      |
+-------------------------+    +-------------------------+    +-------------------------+
```

### 2.2 Physical & Data Link Parameters
- **PCBA PHY**: Microchip LAN8742A RMII Transceiver (100BASE-TX, 100 Mbps, Full Duplex, Auto-Negotiation enabled).
- **MAC Interface**: STM32 Dedicated Ethernet MAC with Descriptor-based Scatter-Gather DMA.
- **Memory Placement**: Ethernet DMA descriptors and packet buffers are placed in `SRAMAHB` (`0x30000000`) with D-Cache management (MPU configured as Non-Cacheable / Device or explicit cache invalidate/clean operations).
- **IP Addressing**:
  - `DHCP` (Default): Dynamic IP from central router/host.
  - `DHCP_WITH_FALLBACK`: Automatic fallback to static subnet (`192.168.1.100 + NodeID`) if DHCP lease fails within 5000 ms.
  - `STATIC`: Fixed manual address assignment.

---

## 3. Transport Layer & Port Mapping

To achieve both **ultra-low-latency streaming** and **deterministic command execution**, a hybrid dual-transport model is utilized:

```
+---------------------+-------------------+-------------+----------------------------------------------+
| Service             | Transport / Mode  | Port        | Purpose                                      |
+---------------------+-------------------+-------------+----------------------------------------------+
| Discovery / Probe   | UDP Broadcast     | 50000       | Node advertisement & Host probe / ping       |
| High-Speed Stream   | UDP Unicast       | 50001       | Zero-copy batched telemetry (Node -> Host)   |
| Control & Config    | TCP / Netconn     | 50002       | Reliable RPC commands & ACKs (Host <-> Node) |
| Time Sync (Future)  | UDP / PTP         | 123 / 319   | Sub-microsecond clock synchronization        |
+---------------------+-------------------+-------------+----------------------------------------------+
```

### 3.1 UDP Broadcast Discovery (Port 50000)
- **Node Heartbeat**: Each node broadcasts a compact 32-byte heartbeat packet at 1 Hz to `255.255.255.255`.
- **Host Probe**: The host can broadcast a `DISCOVERY_PING` packet at startup. All active nodes immediately respond with a `DISCOVERY_PONG` unicast packet to eliminate discovery latency.

### 3.2 High-Speed Telemetry Streaming (Port 50001)
- **Zero Head-of-Line Blocking**: High-speed sensor samples are packetized into UDP datagrams. If an intermediate packet is lost in transit, subsequent packets are still delivered immediately without retransmission delays.
- **Sequence Tracking**: Every datagram contains a 32-bit monotonic sequence number to allow the host to compute real-time packet loss rates and jitter statistics.

### 3.3 TCP Control & Parameter RPC (Port 50002)
- **Persistent or On-Demand Connection**: Host connects via TCP to send commands (e.g. `START_STREAM`, `SET_GAIN`, `SET_SAMPLING_RATE`, `TRIGGER_CALIBRATION`).
- **Guaranteed Delivery & Acknowledgement**: Every command yields an explicit `ACK` or `NACK` packet containing status error codes and returned data payloads.

---

## 4. Binary Wire Protocol Specification

All communications use packed binary structs with Little-Endian byte order (native to Cortex-M7 and x86_64/ARM64 Linux hosts). No string-based serialization (JSON, XML) is used on high-frequency paths to guarantee deterministic, zero-allocation serialization.

### 4.1 Common 16-Byte Message Header (`PE_Header`)

Every packet (UDP and TCP) begins with the standard 16-byte header:

```
 0                   1                   2                   3
 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
|          Magic (0x5045)       |    Version    |     Flags     |
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
|            Node ID            |          Message Type         |
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
|                         Sequence Number                       |
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
|         Payload Length        |             CRC16             |
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
|                        Payload Data (0..N)                    |
|                               ...                             |
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
```

#### C/C++ Header Definition

```cpp
#pragma once
#include <stdint.h>

#pragma pack(push, 1)

struct PE_Header {
    uint16_t magic;         ///< Protocol identifier: 0x5045 ('P', 'E')
    uint8_t  version;       ///< Protocol version: currently 0x01
    uint8_t  flags;         ///< Bitmask: 0x01 = ACK Req, 0x02 = Error, 0x04 = Compressed
    uint16_t node_id;       ///< Originating Node ID (1..65535)
    uint16_t msg_type;      ///< Message identifier enum
    uint32_t seq_num;       ///< Monotonically increasing sequence number
    uint16_t payload_len;   ///< Length of following payload in bytes (excludes header)
    uint16_t crc16;         ///< CRC16-CCITT of payload (0x0000 if disabled/offloaded)
};

static_assert(sizeof(PE_Header) == 16, "PE_Header must be exactly 16 bytes");

#pragma pack(pop)
```

### 4.2 Message Types & ID Mapping

```cpp
enum class MessageType : uint16_t {
    // ── System & Discovery (0x0000 - 0x00FF) ──────────────────────
    HEARTBEAT               = 0x0001,   ///< 1 Hz UDP Broadcast from Node
    DISCOVERY_PING          = 0x0002,   ///< Host broadcast probe
    DISCOVERY_PONG          = 0x0003,   ///< Node unicast probe response
    TIME_SYNC_REQ           = 0x0010,   ///< Timestamp synchronization request
    TIME_SYNC_RESP          = 0x0011,   ///< Timestamp synchronization response

    // ── Control & RPC (0x0100 - 0x01FF) ───────────────────────────
    CMD_GET_NODE_INFO       = 0x0100,   ///< Query hardware UID, FW version, uptime
    CMD_GET_NODE_INFO_RESP  = 0x0101,
    CMD_SET_CONFIG          = 0x0102,   ///< Write configuration parameters
    CMD_GET_CONFIG          = 0x0103,   ///< Read configuration parameters
    CMD_GET_CONFIG_RESP     = 0x0104,
    CMD_START_STREAM        = 0x0110,   ///< Begin streaming telemetry (supports FourCC stream_tag)
    CMD_STOP_STREAM         = 0x0111,   ///< Stop streaming telemetry (supports FourCC stream_tag)
    CMD_GET_STREAMS         = 0x0120,   ///< Query registered channel stream catalog
    CMD_GET_STREAMS_RESP    = 0x0121,   ///< Node stream catalog response
    CMD_REBOOT              = 0x01F0,   ///< Software reset / DFU trigger
    CMD_ACK                 = 0x01FE,   ///< Positive response
    CMD_NACK                = 0x01FF,   ///< Negative response with error code

    // ── High-Speed Telemetry (0x0200 - 0x02FF) ────────────────────
    STREAM_SENSOR_BATCH     = 0x0200,   ///< High-speed batched raw ADC/IMU samples
    STREAM_STATUS_TELEMETRY = 0x0201,   ///< 10-50 Hz board health (temp, voltage, drops)
    STREAM_EVENT_ALERT      = 0x0202,   ///< Asynchronous threshold trigger or fault
};
```

### 4.3 Payload Struct Definitions

#### 1. Heartbeat Payload (`HEARTBEAT` / `0x0001`)
```cpp
#pragma pack(push, 1)
struct PayloadHeartbeat {
    uint32_t uptime_ms;         ///< Milliseconds since MCU boot
    uint32_t fw_version;        ///< Packed uint32 (Major.Minor.Patch.Build)
    uint8_t  node_state;        ///< 0=Init, 1=Idle, 2=Streaming, 3=Fault
    uint8_t  active_streams;    ///< Bitmask of active streaming channels
    uint16_t vdd_mv;            ///< Supply voltage in mV (e.g. 3300)
    int16_t  core_temp_c_x10;   ///< Junction temperature in 0.1 deg C
    uint16_t reserved;
};
#pragma pack(pop)
```

#### 2. Stream Catalog Inquiry (`CMD_GET_STREAMS_RESP` / `0x0121`)
```cpp
#pragma pack(push, 1)
struct StreamDescriptor {
    uint32_t stream_tag;        ///< 4-character FourCC tag (e.g. 'CNTR')
    char     name[16];          ///< Null-terminated stream name
    uint16_t sample_rate_hz;    ///< Sampling frequency (Hz)
    uint16_t batch_count;       ///< Batch count per packet
    uint16_t channel_count;     ///< Number of channels per sample
    uint16_t sample_type;       ///< SampleType enum (0=INT16, 1=UINT16, 2=INT32, 3=FLOAT32, 4=UINT32)
    uint8_t  is_enabled;        ///< 1 = active, 0 = disabled
    uint8_t  reserved[3];       ///< 32-byte alignment padding
};

struct PayloadGetStreamsResp {
    uint16_t stream_count;      ///< Number of following StreamDescriptor entries
    uint16_t reserved;
};
#pragma pack(pop)
```

#### 3. High-Speed Stream Metadata Header (`StreamPayloadHeader` — 20 Bytes)
```cpp
#pragma pack(push, 1)
struct StreamPayloadHeader {
    uint64_t timestamp_us;      ///< Hardware timer microsecond timestamp of first sample
    uint32_t stream_tag;        ///< 4-character FourCC identifier ('CNTR', 'ADC0', 'IMU0')
    uint16_t sample_rate_hz;    ///< Configured sampling frequency (Hz)
    uint16_t sample_count;      ///< Samples packed in this frame
    uint16_t channel_count;     ///< Number of channels per sample
    uint16_t sample_type;       ///< SampleType enum value
};
#pragma pack(pop)
```

---

## 5. Streaming Performance & Multi-Node Scaling Analysis

### 5.1 The 10 kHz Packetization Principle

At a sampling frequency $F_s = 10\text{ kHz}$ ($10,000$ samples per second per channel):
- **Per-Sample Transmission (1 sample / packet)** requires **10,000 packets per second (pps)** per node.
  - This severely overloads the Cortex-M7 interrupt handler and FreeRTOS task scheduler.
  - Ethernet wire efficiency drops below **15%** due to 82 bytes of packet headers for only 2–16 bytes of data.
- **Batched Transmission (K samples / packet)**: Grouping samples into batch buffers matching Ethernet MTU reduces packet rates to **100–500 pps**.
  - Cortex-M7 CPU load drops to **< 2%**.
  - Wire protocol efficiency increases to **> 94%**.
  - Buffering latency is bounded to a deterministic **2–10 ms** window.

### 5.2 Network Overhead & Ethernet Frame Budget

```
+---------------------------------------------------------------------------------------+
|  Preamble + SFD + IPG  |  Eth L2  |  IPv4   |  UDP   |  PE_Header  |  Payload  |  FCS |
|        20 Bytes        | 14 Bytes | 20 Bytes| 8 Bytes|  16 Bytes   |  N Bytes  |  4B  |
+---------------------------------------------------------------------------------------+
|<----------------- 82 Bytes Overhead -------------------------->|<--- Max 1456B ---->|
```

- **Maximum Unfragmented UDP Payload**: $1500 - 20 (\text{IPv4}) - 8 (\text{UDP}) = 1472$ bytes.
- **Maximum Data Payload per Frame**: $1472 - 16 (\text{PE\_Header}) = 1456$ bytes.

---

### 5.3 Multi-Node Scaling Capacity Table (at 10 kHz Sampling)

The following table evaluates system limits assuming **10 kHz sampling frequency** per node, streaming via UDP over standard switches to a **1 Gbps Linux Host uplink** (~900 Mbps / 112.5 MB/s practical limit):

| Configuration | Sample Size ($S$) | Batched Samples ($K$) | Packet Rate (pps) | Payload Rate / Node | Wire Rate / Node (inc. overhead) | Latency Added | Max Nodes (100M Node Link) | Max Nodes (1 Gbps Host Uplink) |
|:---|:---:|:---:|:---:|:---:|:---:|:---:|:---:|:---:|
| **1 Channel (16-bit ADC)** | 2 bytes | 100 | 100 pps | 20 KB/s | 170 kbps | 10.0 ms | 1 | **~5,200 nodes** |
| **2 Channels / 1x Float32** | 4 bytes | 100 | 100 pps | 40 KB/s | 335 kbps | 10.0 ms | 1 | **~2,650 nodes** |
| **4 Channels (16-bit ADC)** | 8 bytes | 100 | 100 pps | 80 KB/s | 665 kbps | 10.0 ms | 1 | **~1,350 nodes** |
| **8 Channels / 6-DOF IMU** | 16 bytes | 50 | 200 pps | 160 KB/s | 1.34 Mbps | 5.0 ms | 1 | **~670 nodes** |
| **16 Channels (16-bit ADC)** | 32 bytes | 40 | 250 pps | 320 KB/s | 2.65 Mbps | 4.0 ms | 1 | **~335 nodes** |
| **32 Channels (Pixel Array)**| 64 bytes | 20 | 500 pps | 640 KB/s | 5.25 Mbps | 2.0 ms | 1 | **~170 nodes** |
| **64 Channels (Sensor Strip)**| 128 bytes | 10 | 1,000 pps | 1.28 MB/s | 10.45 Mbps | 1.0 ms | 1 | **~85 nodes** |
| **Full 100M PHY Saturation** | ~1,100 B | ~12 | 800 pps | 11.0 MB/s | 90.0 Mbps | 1.2 ms | 1 | **~10 nodes** |

---

### 5.4 Bandwidth & Node Calculation Formula

To compute total network throughput for any arbitrary combination of node count ($N$), sample frequency ($F_s$), sample size ($S$), and batch size ($K$):

$$\text{Bandwidth}_{\text{total}} = N \times \left[ \left(F_s \times S\right) + \left(\frac{F_s}{K} \times 82\right) \right] \times 8 \quad \text{[bits/second]}$$

---

## 6. Embedded Node Software Architecture

### 6.1 Component Hierarchy

```
components/net/
├── NetManager.h / .cpp             # Netif lifecycle, link state, DHCP fallback
├── proto/
│   ├── ProtocolTypes.h            # Header structs, enums, message IDs
│   ├── PacketBuilder.h / .cpp     # Header injection, CRC calculation, serialization
│   └── PacketParser.h / .cpp      # Header validation, dispatch table
├── services/
│   ├── DiscoveryService.h / .cpp  # 1 Hz UDP broadcast & discovery responder
│   ├── TelemetryService.h / .cpp  # Zero-copy UDP streaming engine
│   └── CommandService.h / .cpp    # TCP Netconn RPC server
└── lwip_port/                     # lwIP OSAL adaptation & lwipopts.h
```

### 6.2 Zero-Copy Ping-Pong DMA Streaming Pipeline

To eliminate memory copy overhead on Cortex-M7:

```
+-----------------------------+
| Hardware Sensor Source      |
| (ADC / SPI / SAI via DMA)   |
+--------------+--------------+
               |
               v Circular DMA Ring Buffer (Placed in non-cacheable SRAM)
+-----------------------------+-----------------------------+
|      Buffer A (Ping)        |       Buffer B (Pong)       |
| [16B Header] + [Data Slices]| [16B Header] + [Data Slices]|
+-----------------------------+-----------------------------+
               |                             |
    DMA Half-Complete IRQ         DMA Transfer-Complete IRQ
               |                             |
               +--------------+--------------+
                              | FreeRTOS Direct-to-Task Notification
                              v
               +-----------------------------+
               |       TelemetryTask         |
               | - Populates PE_Header seq   |
               | - Zero-copy udp_sendto()    |
               +-----------------------------+
```

1. **Pre-Padded Buffers**: Each DMA buffer pre-allocates 16 bytes at the head for `PE_Header`. The DMA peripheral fills data starting at offset `+16`.
2. **Zero-Copy lwIP Datagram**: `TelemetryService` wraps the buffer pointer in an lwIP `pbuf_custom` (or `PBUF_REF`) and passes it directly to `udp_sendto()`.
3. **No CPU `memcpy`**: Cortex-M7 only writes the 16-byte header metadata; the Ethernet MAC DMA reads the sensor data directly from SRAM.

### 6.3 FreeRTOS Task Priority & Thread Model

| Task Name | Priority | Stack Size | Purpose |
|:---|:---:|:---:|:---|
| `tcpip_thread` | 5 (Realtime) | 4096 B | lwIP core network engine |
| `TelemetryTask`| 4 (High) | 2048 B | Drains DMA ping-pong buffers & transmits UDP packets |
| `NetManager` | 3 (Normal) | 1024 B | Monitors link state, PHY negotiation, and DHCP |
| `CommandTask` | 3 (Normal) | 2048 B | Blocking TCP Netconn server for incoming RPC commands |
| `DiscoveryTask`| 2 (Low) | 1024 B | 1 Hz periodic UDP broadcast of node health |

---

## 7. Host Ingestion Architecture Guidelines

*(Reference design for future host development)*

```
                                  Linux Kernel
                                       |
                   +-------------------+-------------------+
                   | UDP Socket (SO_RCVBUF = 8MB)          | TCP Control Client
                   v                                       v
        +-----------------------+              +-----------------------+
        | Ingestion Thread Loop |              | Command / RPC Worker  |
        | - recvfrom()          |              | - JSON / Binary RPC   |
        | - Header Validation   |              | - Timeout / Retries   |
        +-----------+-----------+              +-----------------------+
                    | Ring Buffer
                    v
        +-----------------------+
        | NumPy / C++ Ingest    |
        | - De-interleaving     |
        | - Storage / HDF5 / DB |
        | - Live GUI / Analysis |
        +-----------------------+
```

- **Socket Buffer Tuning**: Set `SO_RCVBUF` to $\ge 4\text{ MB}$ to prevent kernel UDP drops during OS scheduling bursts.
- **Worker Isolation**: Ingestion worker processes raw bytes directly into pre-allocated circular ring buffers without dynamic heap allocations.

---

## 8. Reliability, Recovery & Edge Cases

| Scenario | Embedded Node Behavior | Host Behavior |
|:---|:---|:---|
| **Ethernet Cable Disconnect** | `NetManager` detects PHY link-down via MII status polling; halts active streaming; enters link-recovery state machine. | Detects missing heartbeats; marks node as `OFFLINE` in registry after 3 missed periods (3 seconds). |
| **DHCP Server Unavailable** | After `dhcp_timeout_ms` (e.g. 5000 ms), falls back to static IP (`192.168.1.100 + NodeID`). | Broadcasts discovery probe across fallback subnet. |
| **Network Congestion / Drop** | Drops are non-blocking on node. Frame drop counter increments. | Detects missing `seq_num`; logs gap in data recording; continues stream without stalls. |
| **TCP Command Disconnect** | `CommandService` closes broken socket and returns to `netconn_accept()` loop. | Client initiates reconnect with exponential backoff. |

---

## 9. Future Extensions (OTA & Precision Time Protocol)

1. **Ethernet XIP Firmware Updates (OTA)**:
   - Leverage the existing two-stage XIP bootloader.
   - Host streams firmware binary via TCP; PCBA writes directly to external Octal-SPI flash (`0x70000000`) and triggers software reset.
2. **Precision Time Protocol (IEEE 1588 / PTP)**:
   - Utilize the STM32H7 hardware Ethernet PTP timestamping engine (`ETH_PTPTSAR`) to correlate multi-node sensor data with sub-microsecond precision across the entire PCBA cluster.
