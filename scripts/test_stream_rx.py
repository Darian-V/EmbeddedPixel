import socket
import struct
import time

def main():
    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    sock.setsockopt(socket.SOL_SOCKET, socket.SO_BROADCAST, 1)
    sock.settimeout(15.0)
    sock.bind(('', 50001))

    print("Listening for FourCC-tagged stream packets on UDP port 50001 (timeout 15s)...")
    count = 0
    last_t = None
    try:
        while count < 10:
            data, addr = sock.recvfrom(2048)
            now = time.time()
            dt = (now - last_t) * 1000 if last_t else 0
            last_t = now

            # 1. Parse PE_Header (16 bytes)
            magic, ver, flags, node_id, msg_type, seq_num, payload_len, crc16 = struct.unpack_from('<HBBHHIHH', data, 0)

            # 2. Parse StreamPayloadHeader (20 bytes at offset 16)
            ts_us, stream_tag_raw, rate_hz, sample_count, ch_count, sample_type = struct.unpack_from('<Q4sHHHH', data, 16)
            stream_tag = stream_tag_raw.decode('ascii', errors='replace')

            # 3. Parse Counter payload (at offset 36)
            counter_val = struct.unpack_from('<I', data, 36)[0]

            print(f"From {addr}: Seq={seq_num:4d} | Tag='{stream_tag}' | Counter={counter_val:6d} | Rate={rate_hz}Hz | Samples={sample_count} | dt={dt:.1f}ms")
            count += 1
    except socket.timeout:
        print("Timeout waiting for packet.")

if __name__ == '__main__':
    main()
