#!/usr/bin/env python3
"""
EmbeddedPixel Ethernet OTA Firmware Updater
Streams firmware binary chunks over TCP to STM32 embedded node, validates
CRC32/CRC16 integrity, triggers automatic bootloader staging swap, and verifies
post-reboot discovery state.
"""

import argparse
import binascii
import socket
import struct
import sys
import time
from pathlib import Path

# ── Protocol Constants ────────────────────────────────────────────────────────
PE_MAGIC = 0x5045  # 'P', 'E'
PE_VERSION = 1

PORT_DISCOVERY = 50000
PORT_COMMAND   = 50002

# Message Types
CMD_GET_NODE_INFO       = 0x0100
CMD_GET_NODE_INFO_RESP  = 0x0101
CMD_OTA_BEGIN           = 0x0130
CMD_OTA_BEGIN_RESP      = 0x0131
CMD_OTA_DATA            = 0x0132
CMD_OTA_DATA_RESP       = 0x0133
CMD_OTA_END             = 0x0134
CMD_OTA_END_RESP        = 0x0135
CMD_OTA_GET_STATUS      = 0x0136
CMD_OTA_GET_STATUS_RESP = 0x0137
CMD_OTA_ABORT           = 0x0138
CMD_REBOOT              = 0x01F0
CMD_ACK                 = 0x01FE
CMD_NACK                = 0x01FF

# Status Codes
STATUS_OK = 0x0000

# Flags
FLAG_ACK_REQUESTED = (1 << 0)
FLAG_IS_RESPONSE   = (1 << 1)
FLAG_ERROR         = (1 << 2)


def crc16_ccitt(data: bytes, seed: int = 0xFFFF) -> int:
    crc = seed
    for b in data:
        crc ^= (b << 8) & 0xFFFF
        for _ in range(8):
            if crc & 0x8000:
                crc = ((crc << 1) ^ 0x1021) & 0xFFFF
            else:
                crc = (crc << 1) & 0xFFFF
    return crc


def make_header(msg_type: int, payload_len: int, seq_num: int, node_id: int = 0, flags: int = 0) -> bytes:
    # PE_Header: magic(2), version(1), flags(1), node_id(2), msg_type(2), seq_num(4), payload_len(2), crc16(2)
    return struct.pack('<HBBHH I H H', PE_MAGIC, PE_VERSION, flags, node_id, msg_type, seq_num, payload_len, 0)


def parse_header(buf: bytes):
    if len(buf) < 16:
        return None
    magic, version, flags, node_id, msg_type, seq_num, payload_len, crc16 = struct.unpack('<HBBHH I H H', buf[:16])
    return {
        'magic': magic,
        'version': version,
        'flags': flags,
        'node_id': node_id,
        'msg_type': msg_type,
        'seq_num': seq_num,
        'payload_len': payload_len,
        'crc16': crc16,
    }


def recv_exact(sock: socket.socket, count: int, timeout: float = 5.0) -> bytes:
    sock.settimeout(timeout)
    data = bytearray()
    while len(data) < count:
        packet = sock.recv(count - len(data))
        if not packet:
            raise ConnectionError("Socket closed prematurely")
        data.extend(packet)
    return bytes(data)


def format_version(ver: int) -> str:
    major = (ver >> 24) & 0xFF
    minor = (ver >> 16) & 0xFF
    patch = (ver >> 8) & 0xFF
    build = ver & 0xFF
    return f"v{major}.{minor}.{patch}.{build}" if build else f"v{major}.{minor}.{patch}"


def run_ota(ip: str, bin_path: Path, target_version: int, auto_reboot: bool = True, chunk_size: int = 1024):
    if not bin_path.exists():
        print(f"Error: Binary file '{bin_path}' not found!")
        sys.exit(1)

    binary_data = bin_path.read_bytes()
    total_size = len(binary_data)
    total_crc32 = binascii.crc32(binary_data) & 0xFFFFFFFF

    print(f"=========================================================")
    print(f"  EmbeddedPixel Ethernet OTA Updater")
    print(f"=========================================================")
    print(f" Target IP:       {ip}:{PORT_COMMAND}")
    print(f" Firmware File:   {bin_path.name} ({total_size:,} bytes)")
    print(f" Target Version:  0x{target_version:08X} ({format_version(target_version)})")
    print(f" Image CRC32:     0x{total_crc32:08X}")
    print(f" Chunk Size:      {chunk_size} bytes")
    print(f"=========================================================\n")

    print(f"Connecting to {ip}:{PORT_COMMAND}...")
    sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    sock.settimeout(5.0)
    try:
        sock.connect((ip, PORT_COMMAND))
    except Exception as e:
        print(f"Failed to connect to node: {e}")
        sys.exit(1)
    print("Connected.")

    seq_num = 1

    # 1. Query Current Node Info
    print("Querying current node info...")
    hdr = make_header(CMD_GET_NODE_INFO, 0, seq_num)
    sock.sendall(hdr)
    seq_num += 1

    resp_hdr_bytes = recv_exact(sock, 16)
    resp_hdr = parse_header(resp_hdr_bytes)
    resp_payload = recv_exact(sock, resp_hdr['payload_len'])
    if resp_hdr['msg_type'] == CMD_GET_NODE_INFO_RESP and len(resp_payload) >= 20:
        cur_fw_ver = struct.unpack('<I', resp_payload[16:20])[0]
        print(f" -> Active Node Firmware Version: 0x{cur_fw_ver:08X} ({format_version(cur_fw_ver)})\n")

    # 2. Send CMD_OTA_BEGIN
    print("Initiating OTA transfer (CMD_OTA_BEGIN)...")
    flags = 0x01 if auto_reboot else 0x00
    begin_payload = struct.pack('<IIIHH', total_size, total_crc32, target_version, chunk_size, flags)
    hdr = make_header(CMD_OTA_BEGIN, len(begin_payload), seq_num)
    sock.sendall(hdr + begin_payload)
    seq_num += 1

    resp_hdr_bytes = recv_exact(sock, 16, timeout=15.0) # Flash erase may take several seconds
    resp_hdr = parse_header(resp_hdr_bytes)
    resp_payload = recv_exact(sock, resp_hdr['payload_len'])

    if resp_hdr['msg_type'] != CMD_OTA_BEGIN_RESP:
        print(f"OTA Begin failed: Unexpected response type 0x{resp_hdr['msg_type']:04X}")
        sock.close()
        sys.exit(1)

    status_code, ack_chunk_size, max_kb = struct.unpack('<IHH', resp_payload[:8])
    if status_code != STATUS_OK:
        print(f"OTA Begin rejected by node! Status code: 0x{status_code:04X}")
        sock.close()
        sys.exit(1)

    chunk_size = ack_chunk_size
    print(f"Node accepted OTA begin (Chunk size: {chunk_size} B, Max Capacity: {max_kb} KB)\n")

    # 3. Stream Data Chunks
    print("Streaming firmware chunks to external flash staging area...")
    offset = 0
    start_time = time.time()

    while offset < total_size:
        chunk = binary_data[offset:offset + chunk_size]
        chunk_len = len(chunk)
        chunk_crc = crc16_ccitt(chunk)

        # PayloadOtaData: offset(4), chunk_len(2), chunk_crc16(2) + chunk bytes
        data_hdr = struct.pack('<IHH', offset, chunk_len, chunk_crc)
        payload = data_hdr + chunk
        hdr = make_header(CMD_OTA_DATA, len(payload), seq_num)

        sock.sendall(hdr + payload)
        seq_num += 1

        # Wait for chunk ACK
        ack_hdr_bytes = recv_exact(sock, 16)
        ack_hdr = parse_header(ack_hdr_bytes)
        ack_payload = recv_exact(sock, ack_hdr['payload_len'])

        if ack_hdr['msg_type'] != CMD_ACK:
            print(f"\nChunk write failed at offset {offset:,}! Status: 0x{ack_hdr['msg_type']:04X}")
            sock.close()
            sys.exit(1)

        offset += chunk_len
        elapsed = max(time.time() - start_time, 0.001)
        speed_kb = (offset / 1024.0) / elapsed
        pct = (offset / total_size) * 100.0

        bar_width = 30
        filled = int(bar_width * (offset / total_size))
        bar = '=' * filled + '>' + ' ' * (bar_width - filled - 1) if filled < bar_width else '=' * bar_width
        print(f"\r [{bar}] {pct:5.1f}% ({offset:,} / {total_size:,} bytes, {speed_kb:.1f} KB/s)", end='', flush=True)

    total_time = time.time() - start_time
    avg_speed = (total_size / 1024.0) / total_time
    print(f"\nTransfer complete! {total_size:,} bytes sent in {total_time:.2f}s (Avg {avg_speed:.1f} KB/s)\n")

    # 4. Send CMD_OTA_END
    print("Finalizing OTA transfer and verifying staging CRC32 (CMD_OTA_END)...")
    end_payload = struct.pack('<IB3s', total_crc32, 1 if auto_reboot else 0, b'\x00\x00\x00')
    hdr = make_header(CMD_OTA_END, len(end_payload), seq_num)
    sock.sendall(hdr + end_payload)

    try:
        ack_hdr_bytes = recv_exact(sock, 16, timeout=10.0)
        ack_hdr = parse_header(ack_hdr_bytes)
        ack_payload = recv_exact(sock, ack_hdr['payload_len'])
        if ack_hdr['msg_type'] == CMD_ACK:
            print("OTA verification SUCCESSFUL! Node staged image and set PENDING_INSTALL.")
        else:
            print(f"OTA verification FAILED on node (MsgType: 0x{ack_hdr['msg_type']:04X})")
            sys.exit(1)
    except Exception as e:
        print(f"Node closed connection (Rebooting into Bootloader): {e}")

    sock.close()

    # 5. Monitor Reboot & Discovery Pong
    if auto_reboot:
        print("\nWaiting for node to reboot, copy image in bootloader, and come back online...")
        udp_sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        udp_sock.setsockopt(socket.SOL_SOCKET, socket.SO_BROADCAST, 1)
        udp_sock.settimeout(1.0)

        deadline = time.time() + 20.0
        node_discovered = False
        while time.time() < deadline:
            # Send periodic discovery ping
            ping_payload = struct.pack('<IH2s', 0x12345678, 0, b'\x00\x00')
            ping_hdr = make_header(0x0002, len(ping_payload), 1)
            try:
                udp_sock.sendto(ping_hdr + ping_payload, ('<broadcast>', PORT_DISCOVERY))
            except Exception:
                pass

            try:
                data, addr = udp_sock.recvfrom(256)
                if len(data) >= 16:
                    hdr = parse_header(data)
                    if hdr and hdr['msg_type'] in (0x0001, 0x0003): # Heartbeat or Pong
                        payload = data[16:]
                        if hdr['msg_type'] == 0x0001 and len(payload) >= 8:
                            uptime, fw_ver = struct.unpack('<II', payload[:8])
                        elif hdr['msg_type'] == 0x0003 and len(payload) >= 28:
                            # PayloadDiscoveryPong: challenge(4), nid(2), nstate(2), ip(4), mac(6), rsv(2), fw(4), up(4)
                            _, _, _, _, _, _, _, _, _, _, _, fw_ver, uptime = struct.unpack('<IHHI 6B H II', payload[:28])
                        else:
                            continue

                        print(f"\n[+] Node is ONLINE at {addr[0]}!")
                        print(f"    Firmware Version: 0x{fw_ver:08X} ({format_version(fw_ver)})")
                        print(f"    Uptime:           {uptime} ms")
                        if fw_ver == target_version:
                            print(f"\n>>> OTA UPDATE SUCCESSFULLY VERIFIED! <<<")
                        else:
                            print(f"\n[!] Warning: Reported version 0x{fw_ver:08X} differs from target 0x{target_version:08X}")
                        node_discovered = True
                        break
            except socket.timeout:
                print(".", end='', flush=True)

        udp_sock.close()
        if not node_discovered:
            print("\nTimed out waiting for node discovery heartbeat.")


def main():
    parser = argparse.ArgumentParser(description="EmbeddedPixel Ethernet OTA Firmware Updater")
    parser.add_argument("--ip", default="192.168.1.111", help="Target node IP address")
    parser.add_argument("--bin", required=True, type=Path, help="Path to .bin firmware image")
    parser.add_argument("--version", default="0x00010100", help="Target version hex (e.g. 0x00010100 for v1.1.0)")
    parser.add_argument("--no-reboot", action="store_true", help="Do not trigger auto-reboot after transfer")
    parser.add_argument("--chunk-size", type=int, default=1024, help="Chunk size in bytes (default 1024)")

    args = parser.parse_args()
    target_ver = int(args.version, 0)
    run_ota(args.ip, args.bin, target_ver, not args.no_reboot, args.chunk_size)


if __name__ == '__main__':
    main()
