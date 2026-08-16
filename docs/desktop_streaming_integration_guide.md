# Desktop Telemetry Streaming Integration Guide

**Target Audience:** Desktop Software Development Team / UI Developers  
**Protocol Version:** 1.0 (PE Protocol)  
**Document Status:** Approved / Active  
**Embedded Firmware Target:** STM32H7 Series (e.g. Nucleo-H7S3L8, PixelJam)

---

## 1. Overview & Architecture

The STM32 embedded nodes stream high-speed sensor and diagnostic telemetry to host desktop applications over Ethernet using a lightweight, zero-copy binary UDP protocol.

### Key Characteristics
* **Transport:** UDP (Port `50001` — Unicast or Broadcast)
* **Discovery & Heartbeat Port:** UDP `50000`
* **Command & Control RPC Port:** TCP `50002`
* **Byte Order:** **Little-Endian** throughout all headers and payloads.
* **Alignment:** Packed binary structs (`#pragma pack(1)`) with natural field alignment.
* **FourCC Stream Labeling:** Each telemetry stream packet carries a 4-character **FourCC identifier** (e.g. `'CNTR'`, `'ADC0'`, `'IMU0'`), allowing host software to dynamically identify, route, and decode channels without hardcoding ports or IP addresses.
* **Autonomous Node Scheduling:** Each stream runs at its own configured sampling frequency. For telemetry rates ($\le 100\text{ Hz}$), the node transmits **1 packet per sampling tick** ($\text{Packet Period} = 1000 / \text{Rate Hz}$).

---

## 2. Binary Wire Protocol Specification

Every UDP telemetry datagram arriving on port `50001` consists of three contiguous blocks:

```
+------------------------------------+------------------------------------+-------------------------+
|      1. PE_Header (16 Bytes)       | 2. StreamPayloadHeader (20 Bytes)  | 3. Raw Data Payload     |
|   (Common Packet Routing / Sync)   |    (Stream Metadata & FourCC Tag)  |   (N Channels x Samples)|
+------------------------------------+------------------------------------+-------------------------+
0                                    16                                   36                   36 + N
```

---

### 2.1 Common Header (`PE_Header` — 16 Bytes)

All protocol packets (discovery, telemetry, control) begin with this standard 16-byte header:

| Offset | Type | Field | Description | Expected Value |
|:---|:---|:---|:---|:---|
| `0..1` | `uint16_t` | `magic` | Protocol Sync Marker (`'P'`, `'E'`) | `0x5045` (`b'\x45\x50'`) |
| `2` | `uint8_t` | `version` | Protocol Version | `0x01` |
| `3` | `uint8_t` | `flags` | Bitmask: `0x08` = Has Timestamp | `0x08` |
| `4..5` | `uint16_t` | `node_id` | Originating STM32 Board Node ID | `1 .. 65535` |
| `6..7` | `uint16_t` | `msg_type` | Message Type Identifier | `0x0200` (`STREAM_SENSOR_BATCH`) |
| `8..11` | `uint32_t` | `seq_num` | Monotonically increasing sequence number | `0, 1, 2, 3, ...` |
| `12..13` | `uint16_t` | `payload_len` | Payload length following this 16B header | `sizeof(StreamPayloadHeader) + data_bytes` |
| `14..15` | `uint16_t` | `crc16` | Optional CRC16-CCITT (`0x0000` if disabled) | `0x0000` |

---

### 2.2 Stream Metadata Header (`StreamPayloadHeader` — 20 Bytes)

The stream header immediately follows `PE_Header` (starting at byte offset `16`):

| Offset | Type | Field | Description | Example (`'CNTR'`) |
|:---|:---|:---|:---|:---|
| `16..23` | `uint64_t` | `timestamp_us` | Microsecond hardware timer timestamp | e.g. `12543000` µs |
| `24..27` | `char[4]` / `uint32_t` | `stream_tag` | **4-character FourCC channel identifier** | `'CNTR'` (`0x52544E43`) |
| `28..29` | `uint16_t` | `sample_rate_hz` | Sampling frequency in Hz | `10` |
| `30..31` | `uint16_t` | `sample_count` | Number of samples in this frame | `1` |
| `32..33` | `uint16_t` | `channel_count` | Number of channels per sample | `1` |
| `34..35` | `uint16_t` | `sample_type` | Data type enum (see table below) | `4` (`UINT32`) |

---

### 2.3 Sample Type Encoding (`sample_type`)

| Value | Name | C Type | Size per Element | Description |
|:---|:---|:---|:---:|:---|
| `0` | `INT16` | `int16_t` | 2 bytes | Signed 16-bit integer |
| `1` | `UINT16` | `uint16_t` | 2 bytes | Unsigned 16-bit integer |
| `2` | `INT32` | `int32_t` | 4 bytes | Signed 32-bit integer |
| `3` | `FLOAT32` | `float` | 4 bytes | Single-precision IEEE 754 float |
| `4` | `UINT32` | `uint32_t` | 4 bytes | Unsigned 32-bit integer *(Used by Counter)* |

$$\text{Data Payload Size (Bytes)} = \text{sample\_count} \times \text{channel\_count} \times \text{sizeof}(\text{sample\_type})$$

---

## 3. FourCC Stream Tag Registry

| FourCC Tag | ASCII | Channels | Sample Type | Default Rate | Description |
|:---|:---:|:---:|:---:|:---:|:---|
| `0x52544E43` | `'CNTR'` | 1 | `UINT32` | 10 Hz | Monotonic Diagnostic Counter (`0, 1, 2, 3, ...`) |
| `0x30434441` | `'ADC0'` | 8 | `UINT16` | 1000 Hz | Multi-channel raw ADC capture |
| `0x30554D49` | `'IMU0'` | 6 | `FLOAT32` | 100 Hz | 6-DOF IMU (Accel X,Y,Z + Gyro X,Y,Z) |
| `0x4C584950` | `'PIXL'` | N | `UINT16` | 30 Hz | Pixel Sensor Array frame |

---

## 4. UI Stream Control & RPC Interface (TCP Port 50002)

Desktop host software and UI control panels interact with the node via TCP port `50002` to discover channels and dynamically adjust streaming data rates.

```
+-------------------------------------------------------------------------------+
|                            COMMAND RPC (TCP : 50002)                          |
|                                                                               |
|  [Host UI]  --- CMD_GET_STREAMS (0x0120) ------------>  [STM32 Node]          |
|             <-- CMD_GET_STREAMS_RESP (0x0121) --------  (Returns Catalog)     |
|                                                                               |
|  [Host UI]  --- CMD_START_STREAM (0x0110, Tag, Rate)->  [STM32 Node]          |
|             <-- CMD_ACK (0x01FE) ---------------------  (Applies Rate / Starts)
|                                                                               |
|  [Host UI]  --- CMD_STOP_STREAM (0x0111, Tag) ------->  [STM32 Node]          |
|             <-- CMD_ACK (0x01FE) ---------------------  (Stops Stream)        |
+-------------------------------------------------------------------------------+
```

---

### 4.1 Discovering Available Channels (`CMD_GET_STREAMS`)

To populate the UI with the node's supported streams:

1. **Send Request:**
   * `msg_type`: `0x0120` (`CMD_GET_STREAMS`)
   * `flags`: `0x01` (`FLAG_ACK_REQUESTED`)
   * `payload_len`: `0`

2. **Receive Response (`CMD_GET_STREAMS_RESP` — `0x0121`):**
   * **Header:** 16-byte `PE_Header` with `msg_type = 0x0121`.
   * **Payload Header (`PayloadGetStreamsResp` — 4 Bytes):**
     * `stream_count` (`uint16_t` at offset 16): Number of streams available.
     * `reserved` (`uint16_t` at offset 18).
   * **Descriptor Array (`StreamDescriptor` — 32 Bytes per stream starting at offset 20):**

| Offset | Type | Field | Description |
|:---|:---|:---|:---|
| `+0..3` | `uint32_t` / `char[4]` | `stream_tag` | 4-character FourCC tag (e.g. `'CNTR'`) |
| `+4..19` | `char[16]` | `name` | Null-terminated human-readable name (e.g. `"Counter"`) |
| `+20..21` | `uint16_t` | `sample_rate_hz` | Current/native sampling frequency in Hz (e.g. `10`) |
| `+22..23` | `uint16_t` | `batch_count` | Batch count per packet (e.g. `1`) |
| `+24..25` | `uint16_t` | `channel_count` | Number of channels per sample (e.g. `1`) |
| `+26..27` | `uint16_t` | `sample_type` | `SampleType` enum value (`4` = `UINT32`) |
| `+28` | `uint8_t` | `is_enabled` | `1` = streaming active, `0` = disabled |
| `+29..31` | `uint8_t[3]` | `reserved` | Padding |

---

### 4.2 Changing Stream Data Rate / Starting Stream (`CMD_START_STREAM`)

To change the streaming frequency of a channel from the UI:

1. **Send Command Packet:**
   * `msg_type`: `0x0110` (`CMD_START_STREAM`)
   * `flags`: `0x01` (`FLAG_ACK_REQUESTED`)
   * `payload_len`: `8` (`PayloadCommand` struct):
     * `cmd_id` (`uint16_t` at offset 16): `0`
     * `param1` (`uint16_t` at offset 18): **Target Frequency in Hz** (e.g. `5`, `10`, `50`, `100`).
     * `param2` (`uint32_t` at offset 20): **FourCC Tag** (e.g. `'CNTR'` = `0x52544E43`) or `0` for all streams.

2. **Packet Transmission Timing:**
   * When rate is set to $R$ Hz, the node immediately adjusts its packet transmission interval to:
     $$\text{Packet Interval (ms)} = \frac{1000}{R}$$
   * **Examples:**
     * Rate = `10 Hz` $\rightarrow$ 1 packet every **100 ms** (10 pkts/sec).
     * Rate = `5 Hz` $\rightarrow$ 1 packet every **200 ms** (5 pkts/sec).
     * Rate = `20 Hz` $\rightarrow$ 1 packet every **50 ms** (20 pkts/sec).
     * Rate = `1 Hz` $\rightarrow$ 1 packet every **1000 ms** (1 pkt/sec).

---

### 4.3 Stopping a Stream (`CMD_STOP_STREAM`)

To disable a stream:
* `msg_type`: `0x0111` (`CMD_STOP_STREAM`)
* `payload_len`: `8`
  * `param2`: **FourCC Tag** (e.g. `'CNTR'`) to stop a specific stream, or `0` to stop all streams.

---

## 5. UI Integration Code Examples

### 5.1 JavaScript / TypeScript (Node.js & Electron UI)

```javascript
const net = require('net');
const dgram = require('dgram');

const PORT_COMMAND = 50002;
const PORT_STREAM  = 50001;

// ── 1. Query Node Streams Catalog (TCP) ──────────────────────────────────────
async function fetchNodeStreams(nodeIp) {
  return new Promise((resolve, reject) => {
    const client = net.createConnection({ host: nodeIp, port: PORT_COMMAND }, () => {
      // Build CMD_GET_STREAMS (0x0120)
      const hdr = Buffer.alloc(16);
      hdr.writeUInt16LE(0x5045, 0);  // Magic 'PE'
      hdr.writeUInt8(1, 2);          // Version
      hdr.writeUInt8(0x01, 3);       // FLAG_ACK_REQUESTED
      hdr.writeUInt16LE(1, 4);       // Node ID
      hdr.writeUInt16LE(0x0120, 6);  // CMD_GET_STREAMS
      hdr.writeUInt32LE(1, 8);       // Seq num
      hdr.writeUInt16LE(0, 12);      // Payload len = 0
      hdr.writeUInt16LE(0, 14);      // CRC16
      client.write(hdr);
    });

    client.on('data', (data) => {
      if (data.length < 20) return;
      const streamCount = data.readUInt16LE(16);
      const streams = [];
      let offset = 20;

      for (let i = 0; i < streamCount && offset + 32 <= data.length; ++i) {
        const streamTag = data.slice(offset, offset + 4).toString('ascii').replace(/\0/g, '');
        const name = data.slice(offset + 4, offset + 20).toString('ascii').replace(/\0/g, '');
        const sampleRateHz = data.readUInt16LE(offset + 20);
        const batchCount = data.readUInt16LE(offset + 22);
        const channelCount = data.readUInt16LE(offset + 24);
        const sampleType = data.readUInt16LE(offset + 26);
        const isEnabled = data.readUInt8(offset + 28) !== 0;

        streams.push({ streamTag, name, sampleRateHz, batchCount, channelCount, sampleType, isEnabled });
        offset += 32;
      }

      client.end();
      resolve(streams);
    });

    client.on('error', reject);
  });
}

// ── 2. Change Stream Rate (TCP) ──────────────────────────────────────────────
async function setStreamRate(nodeIp, streamTagStr, targetRateHz) {
  return new Promise((resolve, reject) => {
    const client = net.createConnection({ host: nodeIp, port: PORT_COMMAND }, () => {
      // FourCC tag buffer
      const tagBuf = Buffer.from(streamTagStr.padEnd(4, '\0').substring(0, 4), 'ascii');
      const tagVal = tagBuf.readUInt32LE(0);

      // PayloadCommand (8 bytes): cmd_id(2B), param1=rate(2B), param2=tag(4B)
      const payload = Buffer.alloc(8);
      payload.writeUInt16LE(0, 0);
      payload.writeUInt16LE(targetRateHz, 2);
      payload.writeUInt32LE(tagVal, 4);

      // PE_Header (16 bytes)
      const hdr = Buffer.alloc(16);
      hdr.writeUInt16LE(0x5045, 0);
      hdr.writeUInt8(1, 2);
      hdr.writeUInt8(0x01, 3);
      hdr.writeUInt16LE(1, 4);
      hdr.writeUInt16LE(0x0110, 6); // CMD_START_STREAM
      hdr.writeUInt32LE(2, 8);
      hdr.writeUInt16LE(8, 12);     // Payload length = 8
      hdr.writeUInt16LE(0, 14);

      client.write(Buffer.concat([hdr, payload]));
    });

    client.on('data', (data) => {
      client.end();
      resolve(true);
    });

    client.on('error', reject);
  });
}

// ── 3. Listen for UDP Telemetry Stream (UDP 50001) ───────────────────────────
function listenTelemetry(onSampleCallback) {
  const socket = dgram.createSocket({ type: 'udp4', reuseAddr: true });

  socket.on('message', (msg, rinfo) => {
    if (msg.length < 36) return;
    const magic = msg.readUInt16LE(0);
    if (magic !== 0x5045) return;

    const seqNum = msg.readUInt32LE(8);
    const timestampUs = msg.readBigUInt64LE(16);
    const streamTag = msg.slice(24, 28).toString('ascii').replace(/\0/g, '');
    const sampleRateHz = msg.readUInt16LE(28);
    const sampleCount = msg.readUInt16LE(30);

    if (streamTag === 'CNTR') {
      const counterVal = msg.readUInt32LE(36);
      onSampleCallback({ streamTag, counterVal, sampleRateHz, seqNum, timestampUs: Number(timestampUs) });
    }
  });

  socket.bind(PORT_STREAM, '0.0.0.0', () => {
    socket.setBroadcast(true);
    console.log(`[UI] Telemetry receiver active on port ${PORT_STREAM}`);
  });
}
```

---

### 5.2 Python Channel Control & Receiver

```python
import socket
import struct

# 1. Update Stream Frequency
def set_stream_frequency(node_ip: str, stream_tag: str, rate_hz: int):
    tag_val = struct.unpack('<I', stream_tag.encode('ascii'))[0]
    s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    s.connect((node_ip, 50002))
    
    hdr = struct.pack('<HBBHHIHH', 0x5045, 1, 1, 1, 0x0110, 1, 8, 0)
    payload = struct.pack('<HHI', 0, rate_hz, tag_val)
    s.sendall(hdr + payload)
    
    resp = s.recv(1024)
    s.close()
    print(f"[+] Updated '{stream_tag}' to {rate_hz}Hz")

# 2. Receive and Verify Timing
def receive_stream():
    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    sock.bind(('0.0.0.0', 50001))
    while True:
        data, addr = sock.recvfrom(2048)
        if len(data) >= 40:
            seq, = struct.unpack_from('<I', data, 8)
            ts, tag_raw, rate, count = struct.unpack_from('<Q4sHH', data, 16)
            tag = tag_raw.decode('ascii')
            if tag == 'CNTR':
                val, = struct.unpack_from('<I', data, 36)
                print(f"[{tag}] Seq={seq} Counter={val} Rate={rate}Hz TS={ts/1e6:.3f}s")
```

---

## 6. Network Configuration & Troubleshooting

1. **IP Addressing**:
   * STM32 attempts **DHCP** first.
   * If no DHCP server responds within 10 seconds, it falls back to static IP `192.168.1.111` (Netmask `255.255.255.0`, Gateway `192.168.1.1`).
2. **Firewall Rules**:
   * Ensure desktop firewall allows incoming UDP traffic on port `50001` (telemetry) and port `50000` (heartbeats), and outbound TCP connections on port `50002`.
3. **Multi-Homed Hosts**:
   * Sockets listening for UDP broadcast should bind to `0.0.0.0:50001` with `SO_REUSEADDR` and `SO_BROADCAST`.
