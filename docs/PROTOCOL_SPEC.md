# EmbeddedPixel Binary Wire Protocol & Metadata Specification

**Document Version:** 2.0.0  
**Target Hardware:** STM32H7 Series (STM32H7RS, STM32H743)  
**Byte Ordering:** Little-Endian (Native to ARM Cortex-M and x86_64/ARM64 hosts)  
**Alignment:** Packed structs (`#pragma pack(1)`)  

---

## 1. Network Transport & Port Mapping

EmbeddedPixel nodes utilize a hybrid UDP/TCP architecture to achieve microsecond-level telemetry streaming while maintaining reliable remote procedure calls and firmware updates:

| Port | Transport | Purpose | Description |
|---|---|---|---|
| **`50000`** | **UDP** | Discovery & Heartbeat | 1 Hz periodic node health broadcasts (`PayloadHeartbeat`) and host discovery ping/pong (`PayloadDiscoveryPing`, `PayloadDiscoveryPong`). |
| **`50001`** | **UDP** | High-Speed Telemetry | Zero-copy multi-channel sensor streaming with 4-character FourCC channel identifiers (`StreamPayloadHeader`). |
| **`50002`** | **TCP** | Control, CLI & OTA | Reliable bidirectional RPC for dynamic stream rate adjustment, unified CLI execution (`CMD_CLI_EXEC`), and RAM-staged OTA firmware updates. |

---

## 2. Common Wire Header (`PE_Header`)

Every packet transmitted across UDP or TCP begins with a contiguous **16-byte fixed header**:

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
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
```

### Struct Layout (16 Bytes)

| Offset | Field | Type | Description | Expected Value |
|---|---|---|---|---|
| `0x00` | `magic` | `uint16_t` | Protocol synchronization marker (`'P'`, `'E'`) | `0x5045` |
| `0x02` | `version` | `uint8_t` | Wire protocol version | `1` |
| `0x03` | `flags` | `uint8_t` | Bitmask: `0x01`=ACK Req, `0x02`=Response, `0x04`=Error, `0x08`=Timestamped | Bitmask |
| `0x04` | `node_id` | `uint16_t` | Target/originating Node ID (`0` = broadcast, `1..65535`) | Node ID |
| `0x06` | `msg_type` | `uint16_t` | Message type identifier (`MessageType`) | e.g. `0x0150` |
| `0x08` | `seq_num` | `uint32_t` | Monotonically incrementing sequence number | `0, 1, 2, ...` |
| `0x0C` | `payload_len` | `uint16_t` | Length of payload immediately following this header | N bytes |
| `0x0E` | `crc16` | `uint16_t` | Header CRC16-CCITT (`poly=0x1021`, seed=`0xFFFF`) over first 14 bytes | Checksum |

---

## 3. Protocol Enums & Status Codes

### Message Types (`MessageType`)

```cpp
enum class MessageType : uint16_t {
    // ── Discovery & System (Port 50000 UDP) ────────────────────────
    HEARTBEAT               = 0x0001,   ///< 1 Hz periodic node broadcast
    DISCOVERY_PING          = 0x0002,   ///< Host probe broadcast
    DISCOVERY_PONG          = 0x0003,   ///< Node unicast probe response
    TIME_SYNC_REQ           = 0x0010,   ///< Clock synchronization request
    TIME_SYNC_RESP          = 0x0011,   ///< Clock synchronization response

    // ── Control & RPC (Port 50002 TCP) ─────────────────────────────
    CMD_GET_NODE_INFO       = 0x0100,   ///< Query hardware UID, versions, features
    CMD_GET_NODE_INFO_RESP  = 0x0101,
    CMD_SET_CONFIG          = 0x0102,   ///< Write configuration parameters
    CMD_GET_CONFIG          = 0x0103,   ///< Read configuration parameters
    CMD_GET_CONFIG_RESP     = 0x0104,
    CMD_START_STREAM        = 0x0110,   ///< Start telemetry channel (FourCC + Rate)
    CMD_STOP_STREAM         = 0x0111,   ///< Stop telemetry channel
    CMD_GET_STREAMS         = 0x0120,   ///< Query registered channel stream catalog
    CMD_GET_STREAMS_RESP    = 0x0121,   ///< Node stream catalog response

    // ── OTA Firmware Updates (Port 50002 TCP) ──────────────────────
    CMD_OTA_BEGIN           = 0x0130,   ///< Initiate OTA session
    CMD_OTA_BEGIN_RESP      = 0x0131,   ///< Node confirms staging capacity
    CMD_OTA_DATA            = 0x0132,   ///< Chunk transmission
    CMD_OTA_DATA_RESP       = 0x0133,   ///< Chunk acknowledgement
    CMD_OTA_END             = 0x0134,   ///< Finalize & trigger full RAM verification
    CMD_OTA_END_RESP        = 0x0135,   ///< Verification result
    CMD_OTA_GET_STATUS      = 0x0136,   ///< Query progress
    CMD_OTA_GET_STATUS_RESP = 0x0137,
    CMD_OTA_ABORT           = 0x0138,   ///< Cancel active transfer

    // ── Unified CLI Execution (Port 50002 TCP) ─────────────────────
    CMD_CLI_EXEC            = 0x0150,   ///< Execute text command string over TCP
    CMD_CLI_EXEC_RESP       = 0x0151,   ///< Text output response

    CMD_REBOOT              = 0x01F0,   ///< Soft reset into bootloader
    CMD_ACK                 = 0x01FE,   ///< Generic success acknowledgement
    CMD_NACK                = 0x01FF,   ///< Generic failure / rejection

    // ── High-Speed Telemetry (Port 50001 UDP) ──────────────────────
    STREAM_SENSOR_BATCH     = 0x0200,   ///< Batched raw sensor/counter datagram
    STREAM_STATUS_TELEMETRY = 0x0201,   ///< Board health telemetry
    STREAM_EVENT_ALERT      = 0x0202,   ///< Asynchronous threshold or fault alert
};
```

### Status Codes (`StatusCode`)

| Code | Value | Name | Description |
|---|---|---|---|
| `0` | `0x0000` | `OK` | Operation completed successfully |
| `1` | `0x0001` | `ERR_INVALID_MAGIC` | Protocol header magic mismatch (`0x5045`) |
| `2` | `0x0002` | `ERR_INVALID_VERSION` | Unsupported protocol version |
| `3` | `0x0003` | `ERR_INVALID_CRC` | Header CRC16 or payload CRC32 checksum failure |
| `4` | `0x0004` | `ERR_UNKNOWN_CMD` | Unrecognized message type or command |
| `5` | `0x0005` | `ERR_INVALID_PAYLOAD` | Malformed or out-of-bounds payload |
| `6` | `0x0006` | `ERR_BUSY` | Resource busy (e.g. OTA update in progress) |
| `7` | `0x0007` | `ERR_FLASH_WRITE` | External flash programming error |
| `8` | `0x0008` | `ERR_FLASH_ERASE` | External flash sector erase failure |
| `9` | `0x0009` | `ERR_IMAGE_TOO_LARGE` | Firmware image exceeds AXI SRAM staging area (160 KB) |
| `10` | `0x000A` | `ERR_OTA_DISABLED` | Node OTA feature locked (`FEAT_OTA_RAM_STAGING` not set) |
| `11` | `0x000B` | `ERR_INCOMPATIBLE_BOARD` | Firmware compiled for a different `BoardId` |
| `12` | `0x000C` | `ERR_INCOMPATIBLE_BOOTLOADER` | Node bootloader version < `min_bootloader_version` |
| `13` | `0x000D` | `ERR_VERSION_DOWNGRADE` | Version downgrade blocked by policy |

---

## 4. Payload Specifications

### 4.1 System & Discovery Payloads (Port 50000 UDP)

#### 1. `PayloadHeartbeat` (16 Bytes, Broadcast at 1 Hz)
```cpp
struct PayloadHeartbeat {
    uint32_t uptime_ms;          // Node uptime in milliseconds
    uint32_t fw_version;         // Packed application version (0x01010000)
    uint8_t  node_state;         // 0=INIT, 1=IDLE, 2=STREAMING, 3=FAULT
    uint8_t  active_streams;     // Bitmask of active streaming channels
    uint16_t vdd_mv;             // Supply voltage in mV (e.g. 3300)
    int16_t  core_temp_c_x10;    // Junction temperature in 0.1 deg C (e.g. 345 = 34.5 C)
    uint16_t feature_flags_low;  // Lower 16 bits of FeatureFlag bitmask
};
```

#### 2. `PayloadDiscoveryPong` (48 Bytes, Node Unicast Response to Ping)
```cpp
struct PayloadDiscoveryPong {
    uint32_t challenge_id;       // Echoed correlation ID from host ping
    uint16_t node_id;            // Node ID
    uint16_t node_state;         // NodeState enum (0=INIT, 1=IDLE, 2=STREAMING, 3=FAULT)
    uint32_t ip_addr;            // Current IP address (Network byte order)
    uint8_t  mac_addr[6];        // Hardware MAC address
    uint16_t board_id;           // Board ID (0x0001 = Nucleo-H7S3L8, 0x0002 = PixelJam-H743)
    uint32_t fw_version;         // Active application version (e.g. 0x01010000)
    uint32_t uptime_ms;          // Uptime in milliseconds
    uint32_t hw_uid[3];          // 96-bit STM32 hardware unique identifier
    uint32_t bootloader_version; // Active bootloader version (e.g. 0x01000000)
    uint32_t feature_flags;      // Active FeatureFlag bitmask
};
```

---

### 4.2 Telemetry Streaming Payloads (Port 50001 UDP)

#### 1. `StreamPayloadHeader` (20 Bytes)
Precedes raw data samples in `STREAM_SENSOR_BATCH` (`0x0200`):

```cpp
struct StreamPayloadHeader {
    uint64_t timestamp_us;       // Hardware timer timestamp (microsecond)
    uint32_t stream_tag;         // 4-character FourCC identifier ('CNTR', 'ADC0')
    uint16_t sample_rate_hz;     // Sampling frequency in Hz
    uint16_t sample_count;       // Number of samples in this packet
    uint16_t channel_count;      // Channels per sample
    uint16_t sample_type;        // SampleType enum value
};
```

#### 2. FourCC Tag Registry & Sample Types
```cpp
constexpr uint32_t STREAM_TAG_COUNTER = 0x52544E43; // "CNTR" (Diagnostic counter)
constexpr uint32_t STREAM_TAG_ADC     = 0x30434441; // "ADC0" (Multi-channel ADC)
constexpr uint32_t STREAM_TAG_IMU     = 0x30554D49; // "IMU0" (6-DOF IMU)
constexpr uint32_t STREAM_TAG_PIXELS  = 0x4C584950; // "PIXL" (Pixel array frame)
constexpr uint32_t STREAM_TAG_TEMP    = 0x504D4554; // "TEMP" (Digital temp sensor)
```

| Sample Type | Name | C Type | Size per Element | Description |
|---|---|---|---|---|
| `0` | `INT16` | `int16_t` | 2 bytes | Signed 16-bit integer |
| `1` | `UINT16` | `uint16_t` | 2 bytes | Unsigned 16-bit integer |
| `2` | `INT32` | `int32_t` | 4 bytes | Signed 32-bit integer |
| `3` | `FLOAT32` | `float` | 4 bytes | Single-precision IEEE 754 float |
| `4` | `UINT32` | `uint32_t` | 4 bytes | Unsigned 32-bit integer (Used by `CNTR`) |

---

### 4.3 Unified CLI & Control Payloads (Port 50002 TCP)

#### 1. `PayloadCliExec` (`0x0150`) & `PayloadCliExecResp` (`0x0151`)
Allows execution of any text CLI command over TCP without requiring serial cables:

```cpp
struct PayloadCliExec {
    uint16_t cmd_len;    // Length of ASCII command text (e.g. "feature enable ota")
    uint16_t flags;      // 0
    char     cmd_text[]; // Command string (not necessarily null-terminated)
};

struct PayloadCliExecResp {
    uint16_t status_code;// StatusCode value (0 = OK)
    uint16_t resp_len;   // Length of ASCII response text
    char     resp_text[];// Formatted response string
};
```

#### 2. `PayloadCommand` (`0x0110` / `0x0111`) & `PayloadAckNack` (`0x01FE` / `0x01FF`)
```cpp
struct PayloadCommand {
    uint16_t cmd_id;     // Sub-command ID
    uint16_t param1;     // Target sampling rate in Hz (for CMD_START_STREAM)
    uint32_t param2;     // FourCC channel tag (e.g. 'CNTR') or 0 for all
};

struct PayloadAckNack {
    uint16_t cmd_id;     // Echoed command ID
    uint16_t status_code;// StatusCode enum value
    uint32_t result_data;// Return value or error details
    uint32_t reserved;
};
```

---

### 4.4 OTA Firmware Update Payloads (Port 50002 TCP)

```cpp
struct PayloadOtaBegin {
    uint32_t image_size;       // Total firmware binary size in bytes
    uint32_t image_crc32;      // IEEE 802.3 CRC32 over entire binary
    uint32_t target_version;   // Target semantic version (e.g. 0x01010000)
    uint16_t chunk_size;       // Requested chunk size (typically 1024)
    uint16_t flags;            // Bit 0: Auto-reboot upon successful verification
};

struct PayloadOtaBeginResp {
    uint32_t status_code;      // 0 = OK
    uint16_t chunk_size_ack;   // Accepted chunk size (1024)
    uint16_t max_image_size_kb;// Max staging capacity (160 KB)
};

struct PayloadOtaData {
    uint32_t offset;           // Byte offset within binary (0, 1024, 2048, ...)
    uint16_t chunk_len;        // Number of payload bytes following this struct
    uint16_t chunk_crc16;      // CRC16-CCITT of chunk data bytes
    uint8_t  chunk_bytes[];    // Raw firmware chunk
};

struct PayloadOtaEnd {
    uint32_t image_crc32;      // Final full image CRC32
    uint8_t  auto_reboot;      // 1 = Reboot immediately into bootloader
    uint8_t  reserved[3];
};
```

---

## 5. Embedded Application Image Header (`AppImageHeader`)

All valid firmware binaries have an **`AppImageHeader` (64 bytes)** linked at offset **`0x0200`** (512 bytes from start of binary, immediately after the Cortex-M vector table):

```
+---------------------------------------------------------------+
| Offset 0x0000 - 0x01FF : Cortex-M Vector Table (512 bytes)    |
+---------------------------------------------------------------+
| Offset 0x0200 - 0x023F : AppImageHeader (64 bytes)            |
+---------------------------------------------------------------+
| Offset 0x0240 - End    : Application Code (.text, .rodata)    |
+---------------------------------------------------------------+
```

### Header Structure (64 Bytes)

| Offset | Field | Type | Description |
|---|---|---|---|
| `0x00` | `magic` | `uint32_t` | Constant `0x45504657` (`"EPFW"`) |
| `0x04` | `header_version` | `uint16_t` | Structure format version (`1`) |
| `0x06` | `board_id` | `uint16_t` | Target Board ID (`0x0001` = Nucleo-H7S3L8) |
| `0x08` | `app_version` | `uint32_t` | Packed semantic version (e.g. `0x01010000`) |
| `0x0C` | `min_bootloader_version` | `uint32_t` | Minimum required bootloader version (`0x01000000`) |
| `0x10` | `feature_flags` | `uint32_t` | Feature capabilities bitmask supported by binary |
| `0x14` | `image_size` | `uint32_t` | Total size of binary image in bytes |
| `0x18` | `image_crc32` | `uint32_t` | IEEE 802.3 CRC32 over the entire binary |
| `0x1C` | `build_timestamp` | `uint32_t` | Unix epoch build timestamp |
| `0x20` | `git_commit` | `uint32_t` | Truncated 32-bit git commit hash |
| `0x24` | `reserved` | `uint8_t[20]`| Reserved for crypto signatures (zero-padded) |
| `0x3C` | `header_crc32` | `uint32_t` | CRC32 of first 60 bytes of this struct |

### Versioning, Board IDs & Feature Flags

#### 1. Packed Semantic Version (32-bit)
Format: `(Major << 24) | (Minor << 16) | (Patch << 8) | Build`
- `0x01000000` = `v1.0.0`
- `0x01010000` = `v1.1.0`
- `0x01010200` = `v1.1.2`

#### 2. Board ID Registry (`BoardId`)
- `0x0001`: `NUCLEO_H7S3L8` (STM32H7S3L8 Cortex-M7 @ 600MHz)
- `0x0002`: `PIXELJAM_H743` (STM32H743ZI Cortex-M7 @ 480MHz)

#### 3. Feature Capabilities Bitmask (`FeatureFlag`)
| Bit | Value | Name | Description |
|---|---|---|---|
| `Bit 0` | `0x00000001` | `FEAT_ETHERNET_LAN8742` | 100Mbps Ethernet PHY active and healthy |
| `Bit 1` | `0x00000002` | `FEAT_TELEMETRY_STREAM` | High-speed UDP streaming engine |
| `Bit 2` | `0x00000004` | `FEAT_TEMP_SENSOR_DTS` | On-chip digital temperature sensor |
| `Bit 3` | `0x00000008` | `FEAT_OTA_RAM_STAGING` | RAM-staged 2-stage bootloader OTA update enabled |
| `Bit 4` | `0x00000010` | `FEAT_OTA_DUAL_BANK` | Dual-bank flash slot swapping |
| `Bit 5` | `0x00000020` | `FEAT_COMPRESSION_LZ4` | LZ4 compressed payload staging |
| `Bit 6` | `0x00000040` | `FEAT_SECURE_BOOT` | Cryptographic ECDSA signature verification |
| `Bit 7` | `0x00000080` | `FEAT_DYNAMIC_RATE` | Dynamic sensor frequency reconfiguration |
| `Bit 8` | `0x00000100` | `FEAT_UART_CLI` | Interactive serial terminal on USART3 |

---

## 6. Checksum Reference Algorithms

### 1. IEEE 802.3 CRC32 (Firmware Images & Headers)
- **Polynomial**: `0xEDB88320` (Reflected)
- **Initial Remainder**: `0xFFFFFFFF`
- **Final XOR**: `0xFFFFFFFF`
- Standard POSIX `cksum` / Python `binascii.crc32(data)`.

### 2. CRC16-CCITT (Packet Headers)
- **Polynomial**: `0x1021` (Normal)
- **Initial Remainder**: `0xFFFF`
- **Final XOR**: `0x0000`
