import socket
import struct

def test_get_streams():
    s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    s.settimeout(3.0)
    print("Connecting to STM32 TCP Command port 50002...")
    s.connect(('192.168.1.111', 50002))
    print("Connected.")

    # 1. Send CMD_GET_STREAMS (0x0120)
    # PE_Header (16B): magic=0x5045, ver=1, flags=1, node_id=1, msg_type=0x0120, seq=1, len=0, crc=0
    hdr = struct.pack('<HBBHHIHH', 0x5045, 1, 1, 1, 0x0120, 1, 0, 0)
    s.sendall(hdr)

    # 2. Receive CMD_GET_STREAMS_RESP (0x0121)
    resp = s.recv(1024)
    print(f"Received response ({len(resp)} bytes)")

    # Parse PE_Header
    magic, ver, flags, node_id, msg_type, seq_num, payload_len, crc16 = struct.unpack_from('<HBBHHIHH', resp, 0)
    print(f"Header: Magic=0x{magic:04X}, MsgType=0x{msg_type:04X}, PayloadLen={payload_len}")

    # Parse PayloadGetStreamsResp (4 bytes at offset 16)
    stream_count, reserved = struct.unpack_from('<HH', resp, 16)
    print(f"Registered Streams Count: {stream_count}")

    # Parse StreamDescriptor array (32 bytes per descriptor starting at offset 20)
    offset = 20
    for i in range(stream_count):
        # struct StreamDescriptor: uint32 stream_tag, char name[16], uint16 rate, uint16 batch, uint16 ch, uint16 type, uint8 enabled, uint8 res[3]
        tag_raw, name_raw, rate_hz, batch, channels, sample_type, enabled = struct.unpack_from('<4s 16s HHHH B', resp, offset)
        tag_str = tag_raw.decode('ascii', errors='replace')
        name_str = name_raw.decode('ascii', errors='replace').rstrip('\x00')
        print(f"  Stream #{i+1}: Tag='{tag_str}' | Name='{name_str}' | Rate={rate_hz}Hz | Batch={batch} | Ch={channels} | Type={sample_type} | Enabled={bool(enabled)}")
        offset += 32

    s.close()

if __name__ == '__main__':
    test_get_streams()
