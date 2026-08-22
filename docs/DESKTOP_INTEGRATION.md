# Desktop Software & UI Integration Guide

**Document Version:** 2.0.0  
**Target Audience:** Desktop Software Engineers, UI/Electron Developers, Test Automation Engineers  
**Related Specification:** [PROTOCOL_SPEC.md](PROTOCOL_SPEC.md)

---

## 1. System Architecture & Channels

Desktop applications communicate with EmbeddedPixel nodes over three high-level interfaces:

```mermaid
graph LR
    subgraph Desktop Application / GUI
        UI_Dev["Device Manager<br/>(Discovery & Status)"]
        UI_Telem["Telemetry & Charting<br/>(UDP Stream Receiver)"]
        UI_CLI["Terminal Console<br/>(Serial COM or TCP CLI)"]
        UI_OTA["OTA Firmware Updater<br/>(RAM-Staged Flashing)"]
    end

    subgraph Network & Hardware Ports
        UART["Serial COM Port<br/>(USART3 @ 115200 8N1)"]
        UDP_Disc["UDP Port 50000<br/>(Heartbeat / Ping-Pong)"]
        UDP_Telem["UDP Port 50001<br/>(Batched FourCC Stream)"]
        TCP_Ctrl["TCP Port 50002<br/>(Control, CLI & OTA)"]
    end

    subgraph STM32 Node Subsystems
        SysCtrl["SystemController<br/>(Features, Versions & State)"]
        CLI["CliEngine<br/>(Unified Command Processor)"]
        Telem["TelemetryService<br/>(Zero-Copy UDP DMA)"]
        OTA["OtaService & Bootloader<br/>(RAM-Staged Octal-Flash Update)"]
    end

    UI_CLI -.->|Interactive Terminal| UART
    UI_CLI -.->|CMD_CLI_EXEC (0x0150)| TCP_Ctrl
    UI_Dev -.->|Heartbeat (1 Hz) & Pong| UDP_Disc
    UI_Telem -.->|STREAM_SENSOR_BATCH (0x0200)| UDP_Telem
    UI_OTA -.->|CMD_OTA_* Transfer| TCP_Ctrl

    UART --> CLI
    TCP_Ctrl --> CLI
    TCP_Ctrl --> OTA
    UDP_Disc --> SysCtrl
    Telem --> UDP_Telem
    CLI --> SysCtrl
```

---

## 2. Interactive CLI Execution (Serial & TCP)

EmbeddedPixel nodes share a **unified command engine** accessible both locally via USB-UART and remotely over TCP:

| Channel | Connection Details | Behavior |
|---|---|---|
| **Serial COM Port** | `115200 baud`, `8 Data Bits`, `1 Stop Bit`, `No Parity` | VT100 interactive prompt (`EmbeddedPixel> `), backspace editing (`\b`), accepts `\r\n` or `\n`. |
| **Remote TCP CLI** | TCP Port `50002` (`CMD_CLI_EXEC` `0x0150`) | Zero-installation remote management; returns raw response text in `PayloadCliExecResp`. |

### Standard Command Reference

| Command | Arguments | Description | Example |
|---|---|---|---|
| `help` / `?` | *None* | Prints list of available commands and syntax | `help` |
| `version` / `info` | *None* | Displays App Version, Bootloader Version, Board ID, Git SHA, and Build Time | `version` |
| `status` | *None* | Displays IP, MAC, Uptime, Core Temperature, Active Streams, and OTA State | `status` |
| `feature list` | *None* | Lists all hardware & software features with `[ENABLED]` / `[DISABLED]` status | `feature list` |
| `feature enable` | `<name>` | Enables a feature at runtime (`ota`, `telemetry`, `dts`, `cli`, `dynrate`) | `feature enable ota` |
| `feature disable`| `<name>` | Disables a feature at runtime (e.g. locks node against OTA updates) | `feature disable ota` |
| `stream start` | `[tag] [rate_hz]` | Starts telemetry streaming (all channels or specific FourCC tag) | `stream start CNTR 50` |
| `stream stop` | `[tag]` | Stops telemetry streaming | `stream stop` |
| `ota status` | *None* | Displays RAM staging buffer state and progress | `ota status` |
| `ota abort` | *None* | Cancels any active OTA transfer and frees staging buffers | `ota abort` |
| `led` | `<period_ms>` | Configures status LED blink period in milliseconds (10–10000 ms) | `led 200` |
| `reboot` / `reset` | *None* | Performs a software reset into the bootloader | `reboot` |

---

## 3. Python Integration Recipes

### 3.1 Remote CLI Execution via TCP
```python
import socket
import struct

def exec_remote_cli(node_ip: str, command: str) -> str:
    sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    sock.settimeout(5.0)
    sock.connect((node_ip, 50002))

    cmd_bytes = command.encode('utf-8')
    payload = struct.pack('<HH', len(cmd_bytes), 0) + cmd_bytes
    hdr = struct.pack('<HBBHH I H H', 0x5045, 1, 0, 0, 0x0150, 1, len(payload), 0)
    
    sock.sendall(hdr + payload)
    resp_hdr = sock.recv(16)
    _, _, _, _, msg_type, _, payload_len, _ = struct.unpack('<HBBHH I H H', resp_hdr)
    
    resp_payload = sock.recv(payload_len)
    status_code, resp_len = struct.unpack('<HH', resp_payload[:4])
    output = resp_payload[4:4 + resp_len].decode('utf-8', errors='replace')
    sock.close()
    return output

# Example:
# print(exec_remote_cli("192.168.1.111", "status"))
```

### 3.2 UDP Discovery & Heartbeat Listener (Port 50000)
```python
import socket
import struct

def listen_discovery():
    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    sock.bind(('0.0.0.0', 50000))
    print("[Discovery] Listening for 1 Hz node heartbeats on port 50000...")

    while True:
        data, addr = sock.recvfrom(256)
        if len(data) >= 32:
            # PE_Header (16B) + PayloadHeartbeat (16B)
            uptime, fw_ver, state, streams, vdd_mv, temp_x10, feat_low = struct.unpack('<IIBB H h H', data[16:32])
            temp_c = temp_x10 / 10.0
            print(f"[Node {addr[0]}] FW: 0x{fw_ver:08X} | Uptime: {uptime/1000.0:.1f}s | Temp: {temp_c:.1f}C | State: {state}")
```

### 3.3 UDP High-Speed Telemetry Subscriber (Port 50001)
```python
import socket
import struct

def listen_telemetry():
    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    sock.bind(('0.0.0.0', 50001))
    print("[Telemetry] Listening for UDP stream on port 50001...")

    while True:
        data, addr = sock.recvfrom(2048)
        if len(data) >= 36:
            seq_num, = struct.unpack('<I', data[8:12])
            # StreamPayloadHeader at offset 16 (20 bytes)
            ts_us, tag_raw, rate_hz, count, channels, stype = struct.unpack('<Q 4s HHH', data[16:36])
            tag = tag_raw.decode('ascii', errors='ignore')

            if tag == 'CNTR' and len(data) >= 40:
                counter_val, = struct.unpack('<I', data[36:40])
                print(f"[{tag}] Seq: {seq_num} | Counter: {counter_val} | Rate: {rate_hz} Hz | Time: {ts_us/1e6:.3f}s")
```

---

## 4. JavaScript / TypeScript (Node.js & Electron)

```javascript
const net = require('net');
const dgram = require('dgram');

// ── 1. Query Node Streams Catalog (TCP 50002) ───────────────────────────────
async function getRegisteredStreams(nodeIp) {
  return new Promise((resolve, reject) => {
    const client = net.createConnection({ host: nodeIp, port: 50002 }, () => {
      const hdr = Buffer.alloc(16);
      hdr.writeUInt16LE(0x5045, 0);  // 'PE'
      hdr.writeUInt8(1, 2);          // Version
      hdr.writeUInt8(0x01, 3);       // ACK requested
      hdr.writeUInt16LE(1, 4);       // Node ID
      hdr.writeUInt16LE(0x0120, 6);  // CMD_GET_STREAMS
      hdr.writeUInt32LE(1, 8);       // Seq
      hdr.writeUInt16LE(0, 12);      // Payload len = 0
      hdr.writeUInt16LE(0, 14);
      client.write(hdr);
    });

    client.on('data', (data) => {
      if (data.length < 20) return;
      const streamCount = data.readUInt16LE(16);
      const streams = [];
      let offset = 20;

      for (let i = 0; i < streamCount && offset + 32 <= data.length; ++i) {
        const tag = data.slice(offset, offset + 4).toString('ascii').replace(/\0/g, '');
        const name = data.slice(offset + 4, offset + 20).toString('ascii').replace(/\0/g, '');
        const sampleRateHz = data.readUInt16LE(offset + 20);
        const isEnabled = data.readUInt8(offset + 28) !== 0;

        streams.push({ tag, name, sampleRateHz, isEnabled });
        offset += 32;
      }
      client.end();
      resolve(streams);
    });

    client.on('error', reject);
  });
}

// ── 2. Listen to UDP Telemetry (Port 50001) ──────────────────────────────────
function startTelemetryListener(onSample) {
  const socket = dgram.createSocket({ type: 'udp4', reuseAddr: true });
  socket.on('message', (msg) => {
    if (msg.length < 36) return;
    const tag = msg.slice(24, 28).toString('ascii').replace(/\0/g, '');
    const rateHz = msg.readUInt16LE(28);
    const tsUs = Number(msg.readBigUInt64LE(16));

    if (tag === 'CNTR' && msg.length >= 40) {
      const counterVal = msg.readUInt32LE(36);
      onSample({ tag, counterVal, rateHz, tsUs });
    }
  });
  socket.bind(50001, '0.0.0.0');
}
```

---

## 5. OTA Firmware Update & Safety Verification

Firmware updates are staged in MCU internal AXI SRAM (up to 160 KB) and flashed into Macronix Octal-SPI External Flash (`0x70000000`) by the 2-stage bootloader upon soft reset.

### Pre-Flight Safety Gates (Desktop GUIs)
Before initiating an OTA transfer, host software must execute three mandatory safety gates:

1. **Board Compatibility Gate**:
   - Verify `node.board_id == firmware_header.board_id`.
   - Reject with `ERR_INCOMPATIBLE_BOARD` (`0x000B`) if mismatched.
2. **Bootloader Minimum Version Gate**:
   - Verify `node.bootloader_version >= firmware_header.min_bootloader_version`.
   - Reject with `ERR_INCOMPATIBLE_BOOTLOADER` (`0x000C`) if node bootloader is too old.
3. **OTA Security Lock Gate**:
   - Verify `(node.feature_flags & FEAT_OTA_RAM_STAGING) != 0`.
   - If locked, prompt the operator to unlock via `feature enable ota` or serial CLI.

### Standalone Python Updater (`scripts/ota_updater.py`)
```bash
# Query node status over TCP
python scripts/ota_updater.py --ip 192.168.1.111 --cli "status"

# Flash firmware image with automatic header parsing and CRC verification
python scripts/ota_updater.py --ip 192.168.1.111 --bin boards/nucleo_h7s3l8/apps/ethernetdev/programming_files/ethernetdev_nucleo_h7s3l8.bin
```
