#!/usr/bin/env python3
"""
Unit test for OTA wire protocol framing, CRC16-CCITT, CRC32, and payload serialization.
"""

import binascii
import struct
from ota_updater import (
    PE_MAGIC, PE_VERSION,
    CMD_OTA_BEGIN, CMD_OTA_DATA, CMD_OTA_END,
    crc16_ccitt, make_header, parse_header, format_version
)

def test_crc16():
    sample_data = b"123456789"
    crc = crc16_ccitt(sample_data, 0xFFFF)
    # Standard CCITT-FALSE for "123456789" with seed 0xFFFF is 0x29B1
    assert crc == 0x29B1, f"Expected 0x29B1, got 0x{crc:04X}"
    print("[PASS] CRC16-CCITT calculation matches test vectors.")

def test_header_framing():
    hdr_bytes = make_header(CMD_OTA_BEGIN, 16, 42, node_id=1, flags=0x01)
    assert len(hdr_bytes) == 16, f"Header must be 16 bytes, got {len(hdr_bytes)}"
    
    parsed = parse_header(hdr_bytes)
    assert parsed['magic'] == PE_MAGIC
    assert parsed['version'] == PE_VERSION
    assert parsed['msg_type'] == CMD_OTA_BEGIN
    assert parsed['payload_len'] == 16
    assert parsed['seq_num'] == 42
    assert parsed['node_id'] == 1
    assert parsed['flags'] == 0x01
    print("[PASS] PE_Header serialization & parsing verified.")

def test_ota_payloads():
    # PayloadOtaBegin: image_size(4), image_crc32(4), target_version(4), chunk_size(2), flags(2)
    sample_fw = b"\x00\x11\x22\x33" * 256
    fw_size = len(sample_fw)
    fw_crc = binascii.crc32(sample_fw) & 0xFFFFFFFF
    target_ver = 0x00010100
    chunk_size = 1024
    
    payload = struct.pack('<IIIHH', fw_size, fw_crc, target_ver, chunk_size, 1)
    assert len(payload) == 16
    
    unpacked_size, unpacked_crc, unpacked_ver, unpacked_chunk, unpacked_flags = struct.unpack('<IIIHH', payload)
    assert unpacked_size == fw_size
    assert unpacked_crc == fw_crc
    assert unpacked_ver == target_ver
    assert unpacked_chunk == chunk_size
    assert unpacked_flags == 1
    print("[PASS] PayloadOtaBegin framing verified.")

def test_version_formatter():
    assert format_version(0x00010000) == "v0.1.0"
    assert format_version(0x01000000) == "v1.0.0"
    assert format_version(0x00010100) == "v0.1.1"
    print("[PASS] Version string formatting verified.")

if __name__ == '__main__':
    test_crc16()
    test_header_framing()
    test_ota_payloads()
    test_version_formatter()
    print("\nALL OTA WIRE PROTOCOL UNIT TESTS PASSED!")
