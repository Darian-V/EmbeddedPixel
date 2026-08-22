#!/usr/bin/env python3
"""
EmbeddedPixel Ethernet OTA Firmware Updater & CLI Client
Streams firmware binary chunks over TCP to STM32 embedded node, validates
CRC32/CRC16 integrity, inspects embedded AppImageHeader metadata, verifies
board/bootloader compatibility, and manages runtime features over Ethernet.
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
CMD_CLI_EXEC            = 0x0150
CMD_CLI_EXEC_RESP       = 0x0151
CMD_REBOOT              = 0x01F0
CMD_ACK                 = 0x01FE
CMD_NACK                = 0x01FF

# Status Codes
STATUS_OK                          = 0x0000
STATUS_ERR_INVALID_MAGIC           = 0x0001
STATUS_ERR_INVALID_VERSION         = 0x0002
STATUS_ERR_INVALID_CRC             = 0x0003
STATUS_ERR_UNKNOWN_CMD             = 0x0004
STATUS_ERR_INVALID_PAYLOAD         = 0x0005
STATUS_ERR_BUSY                    = 0x0006
STATUS_ERR_FLASH_WRITE             = 0x0007
STATUS_ERR_FLASH_ERASE             = 0x0008
STATUS_ERR_IMAGE_TOO_LARGE         = 0x0009
STATUS_ERR_OTA_DISABLED            = 0x000A
STATUS_ERR_INCOMPATIBLE_BOARD      = 0x000B
STATUS_ERR_INCOMPATIBLE_BOOTLOADER = 0x000C
STATUS_ERR_VERSION_DOWNGRADE       = 0x000D

# Flags
FLAG_ACK_REQUESTED = (1 << 0)
FLAG_IS_RESPONSE   = (1 << 1)
FLAG_ERROR         = (1 << 2)

# Image Header Constants
EPFW_MAGIC = 0x45504657  # "EPFW"
APP_HEADER_OFFSET = 0x200 # 512 bytes

FEATURE_NAMES = {
    (1 << 0): "ETHERNET_LAN8742",
    (1 << 1): "TELEMETRY_STREAM",
    (1 << 2): "TEMP_SENSOR_DTS",
    (1 << 3): "OTA_RAM_STAGING",
    (1 << 4): "OTA_DUAL_BANK",
    (1 << 5): "COMPRESSION_LZ4",
    (1 << 6): "SECURE_BOOT",
    (1 << 7): "DYNAMIC_RATE",
    (1 << 8): "UART_CLI",
}

BOARD_NAMES = {
    0x0001: "Nucleo-H7S3L8",
    0x0002: "PixelJam-H743",
}


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


def format_features(flags: int) -> str:
    active = [name for bit, name in FEATURE_NAMES.items() if (flags & bit)]
    return ", ".join(active) if active else "NONE"


def parse_app_image_header(data: bytes):
    if len(data) < APP_HEADER_OFFSET + 64:
        return None
    header_bytes = data[APP_HEADER_OFFSET:APP_HEADER_OFFSET + 64]
    magic, hdr_ver, board_id, app_ver, min_bl_ver, feat_flags, img_size, img_crc, bld_ts, git_sha = struct.unpack(
        '<IHHI I I I I I I', header_bytes[:36]
    )
    if magic != EPFW_MAGIC:
        return None
    return {
        'magic': magic,
        'header_version': hdr_ver,
        'board_id': board_id,
        'board_name': BOARD_NAMES.get(board_id, f"Unknown (0x{board_id:04X})"),
        'app_version': app_ver,
        'min_bootloader_version': min_bl_ver,
        'feature_flags': feat_flags,
        'image_size': img_size,
        'image_crc32': img_crc,
        'build_timestamp': bld_ts,
        'git_commit': git_sha,
    }


def send_cli_command(ip: str, command: str):
    print(f"Connecting to {ip}:{PORT_COMMAND}...")
    sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    sock.settimeout(5.0)
    try:
        sock.connect((ip, PORT_COMMAND))
    except Exception as e:
        print(f"Failed to connect to node: {e}")
        sys.exit(1)

    cmd_bytes = command.encode('utf-8')
    cli_payload = struct.pack('<HH', len(cmd_bytes), 0) + cmd_bytes
    hdr = make_header(CMD_CLI_EXEC, len(cli_payload), 1)

    sock.sendall(hdr + cli_payload)
    resp_hdr_bytes = recv_exact(sock, 16)
    resp_hdr = parse_header(resp_hdr_bytes)
    resp_payload = recv_exact(sock, resp_hdr['payload_len'])

    if resp_hdr['msg_type'] == CMD_CLI_EXEC_RESP and len(resp_payload) >= 4:
        status_code, resp_len = struct.unpack('<HH', resp_payload[:4])
        resp_text = resp_payload[4:4 + resp_len].decode('utf-8', errors='replace')
        print(resp_text)
    else:
        print(f"Unexpected response: MsgType=0x{resp_hdr['msg_type']:04X}")

    sock.close()


def run_ota(ip: str, bin_path: Path, target_version: int, auto_reboot: bool = True, chunk_size: int = 1024):
    if not bin_path.exists():
        print(f"Error: Binary file '{bin_path}' not found!")
        sys.exit(1)

    binary_data = bin_path.read_bytes()
    total_size = len(binary_data)
    total_crc32 = binascii.crc32(binary_data) & 0xFFFFFFFF

    # Check for embedded AppImageHeader
    hdr_info = parse_app_image_header(binary_data)

    print(f"=========================================================")
    print(f"  EmbeddedPixel Ethernet OTA Updater")
    print(f"=========================================================")
    print(f" Target IP:          {ip}:{PORT_COMMAND}")
    print(f" Firmware File:      {bin_path.name} ({total_size:,} bytes)")
    if hdr_info:
        target_version = hdr_info['app_version']
        print(f" Header Format:      Valid Embedded AppImageHeader (Offset 0x200)")
        print(f" Image Version:      0x{target_version:08X} ({format_version(target_version)})")
        print(f" Target Hardware:    {hdr_info['board_name']}")
        print(f" Min Bootloader Ver: 0x{hdr_info['min_bootloader_version']:08X} ({format_version(hdr_info['min_bootloader_version'])})")
        print(f" Built Features:     {format_features(hdr_info['feature_flags'])}")
    else:
        print(f" Target Version:     0x{target_version:08X} ({format_version(target_version)})")
    print(f" Image CRC32:        0x{total_crc32:08X}")
    print(f" Chunk Size:         {chunk_size} bytes")
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

    # 1. Query Current Node Info & Features
    print("Querying node information and active features...")
    hdr = make_header(CMD_GET_NODE_INFO, 0, seq_num)
    sock.sendall(hdr)
    seq_num += 1

    resp_hdr_bytes = recv_exact(sock, 16)
    resp_hdr = parse_header(resp_hdr_bytes)
    resp_payload = recv_exact(sock, resp_hdr['payload_len'])

    if resp_hdr['msg_type'] == CMD_GET_NODE_INFO_RESP and len(resp_payload) >= 20:
        # PayloadDiscoveryPong: challenge(4), nid(2), nstate(2), ip(4), mac(6), board_id(2), fw(4), uptime(4), uid(12), bl_ver(4), feat_flags(4)
        if len(resp_payload) >= 48:
            _, _, _, _, _, board_id, cur_fw_ver, uptime, _, _, _, bl_ver, feat_flags = struct.unpack(
                '<IHHI 6B H I I III I I', resp_payload[:48]
            )
            print(f" -> Active Node Board:       {BOARD_NAMES.get(board_id, f'0x{board_id:04X}')}")
            print(f" -> Active App Version:      0x{cur_fw_ver:08X} ({format_version(cur_fw_ver)})")
            print(f" -> Active Bootloader:       0x{bl_ver:08X} ({format_version(bl_ver)})")
            print(f" -> Active Feature Flags:    0x{feat_flags:08X} ({format_features(feat_flags)})\n")

            # Pre-flight compatibility checks
            if hdr_info:
                if board_id != hdr_info['board_id']:
                    print(f"[!] ERROR: Target node board ({BOARD_NAMES.get(board_id, hex(board_id))}) does not match firmware board ({hdr_info['board_name']})!")
                    sock.close()
                    sys.exit(1)
                if bl_ver < hdr_info['min_bootloader_version']:
                    print(f"[!] ERROR: Node bootloader ({format_version(bl_ver)}) is older than minimum required ({format_version(hdr_info['min_bootloader_version'])})!")
                    sock.close()
                    sys.exit(1)

            if not (feat_flags & (1 << 3)):  # FEAT_OTA_RAM_STAGING
                print("[!] ERROR: OTA update capability is currently DISABLED / LOCKED on this node!")
                print("    Unlock it via serial COM port or with: python scripts/ota_updater.py --ip <IP> --cli \"feature enable ota\"\n")
                sock.close()
                sys.exit(1)
        else:
            cur_fw_ver = struct.unpack('<I', resp_payload[16:20])[0]
            print(f" -> Active Node Firmware Version: 0x{cur_fw_ver:08X} ({format_version(cur_fw_ver)})\n")

    # 2. Send CMD_OTA_BEGIN
    print("Initiating OTA transfer (CMD_OTA_BEGIN)...")
    flags = 0x01 if auto_reboot else 0x00
    begin_payload = struct.pack('<IIIHH', total_size, total_crc32, target_version, chunk_size, flags)
    hdr = make_header(CMD_OTA_BEGIN, len(begin_payload), seq_num)
    sock.sendall(hdr + begin_payload)
    seq_num += 1

    resp_hdr_bytes = recv_exact(sock, 16, timeout=15.0)
    resp_hdr = parse_header(resp_hdr_bytes)
    resp_payload = recv_exact(sock, resp_hdr['payload_len'])

    if resp_hdr['msg_type'] != CMD_OTA_BEGIN_RESP:
        print(f"OTA Begin failed: Unexpected response type 0x{resp_hdr['msg_type']:04X}")
        sock.close()
        sys.exit(1)

    status_code, ack_chunk_size, max_kb = struct.unpack('<IHH', resp_payload[:8])
    if status_code != STATUS_OK:
        err_names = {
            STATUS_ERR_OTA_DISABLED: "OTA updates are disabled by node security policy",
            STATUS_ERR_IMAGE_TOO_LARGE: "Image exceeds node RAM staging capacity",
            STATUS_ERR_INCOMPATIBLE_BOARD: "Incompatible board ID",
            STATUS_ERR_INCOMPATIBLE_BOOTLOADER: "Node bootloader is too old",
        }
        print(f"OTA Begin rejected by node! Status code: 0x{status_code:04X} ({err_names.get(status_code, 'Unknown Error')})")
        sock.close()
        sys.exit(1)

    chunk_size = ack_chunk_size
    print(f"Node accepted OTA begin (Chunk size: {chunk_size} B, Max Capacity: {max_kb} KB)\n")

    # 3. Stream Data Chunks
    print("Streaming firmware chunks to MCU internal AXI SRAM staging area...")
    offset = 0
    start_time = time.time()

    while offset < total_size:
        chunk = binary_data[offset:offset + chunk_size]
        chunk_len = len(chunk)
        chunk_crc = crc16_ccitt(chunk)

        data_hdr = struct.pack('<IHH', offset, chunk_len, chunk_crc)
        payload = data_hdr + chunk
        hdr = make_header(CMD_OTA_DATA, len(payload), seq_num)

        sock.sendall(hdr + payload)
        seq_num += 1

        ack_hdr_bytes = recv_exact(sock, 16)
        ack_hdr = parse_header(ack_hdr_bytes)
        ack_payload = recv_exact(sock, ack_hdr['payload_len'])

        if ack_hdr['msg_type'] != CMD_ACK:
            print(f"\nChunk write rejected at offset {offset:,}! Status: 0x{ack_hdr['msg_type']:04X}")
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
        print("\nWaiting for node to reboot, program flash in bootloader, and come back online...")
        udp_sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        udp_sock.setsockopt(socket.SOL_SOCKET, socket.SO_BROADCAST, 1)
        udp_sock.settimeout(1.0)

        deadline = time.time() + 25.0
        node_discovered = False
        while time.time() < deadline:
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
                    if hdr and hdr['msg_type'] in (0x0001, 0x0003):
                        payload = data[16:]
                        if hdr['msg_type'] == 0x0001 and len(payload) >= 8:
                            uptime, fw_ver = struct.unpack('<II', payload[:8])
                        elif hdr['msg_type'] == 0x0003 and len(payload) >= 28:
                            if len(payload) >= 48:
                                _, _, _, _, _, r_board_id, fw_ver, uptime, _, _, _, r_bl_ver, r_feat = struct.unpack(
                                    '<IHHI 6B H I I III I I', payload[:48]
                                )
                            else:
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
    parser = argparse.ArgumentParser(description="EmbeddedPixel Ethernet OTA Firmware Updater & CLI Client")
    parser.add_argument("--ip", default="192.168.1.111", help="Target node IP address")
    parser.add_argument("--bin", type=Path, help="Path to .bin firmware image")
    parser.add_argument("--version", default="0x00010100", help="Target version hex (overridden if binary has header)")
    parser.add_argument("--no-reboot", action="store_true", help="Do not trigger auto-reboot after transfer")
    parser.add_argument("--chunk-size", type=int, default=1024, help="Chunk size in bytes (default 1024)")
    parser.add_argument("--cli", type=str, help="Send interactive CLI command over TCP (e.g. --cli 'feature list')")

    args = parser.parse_args()

    if args.cli:
        send_cli_command(args.ip, args.cli)
        return

    if not args.bin:
        print("Error: --bin <path/to/firmware.bin> is required when not using --cli.")
        parser.print_help()
        sys.exit(1)

    target_ver = int(args.version, 0)
    run_ota(args.ip, args.bin, target_ver, not args.no_reboot, args.chunk_size)


if __name__ == '__main__':
    main()
