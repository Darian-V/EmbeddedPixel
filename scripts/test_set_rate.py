import socket
import struct
import time

def set_stream_rate(target_hz, stream_tag='CNTR'):
    # Convert 4-char string to uint32 FourCC
    tag_val = struct.unpack('<I', stream_tag.encode('ascii'))[0]
    
    s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    s.settimeout(3.0)
    s.connect(('192.168.1.111', 50002))
    
    # PE_Header (16B): magic=0x5045, ver=1, flags=1, node_id=1, msg_type=0x0110 (CMD_START_STREAM), seq=1, len=8, crc=0
    hdr = struct.pack('<HBBHHIHH', 0x5045, 1, 1, 1, 0x0110, 1, 8, 0)
    # PayloadCommand (8B): cmd_id=0, param1=target_hz, param2=tag_val (FourCC)
    payload = struct.pack('<HHI', 0, target_hz, tag_val)
    s.sendall(hdr + payload)
    
    resp = s.recv(1024)
    s.close()
    print(f"Sent rate update to {target_hz}Hz for stream '{stream_tag}'. ACK received.")

if __name__ == '__main__':
    # Test setting to 5 Hz (200ms packet interval)
    set_stream_rate(5, 'CNTR')
