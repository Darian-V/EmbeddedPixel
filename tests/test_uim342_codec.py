"""
Verification test suite for UIROBOT UIM342 SimpleCAN 3.0 protocol codec and frame serialization.
Validates ID calculation, payload bit-packing, and response decoding against Manual V6.05.
"""

def encode_can_id(producer_id: int, consumer_id: int, cw: int, req_ack: bool = True) -> int:
    sid = (((producer_id & 0x1F) << 6) | ((consumer_id & 0x1F) << 1)) & 0x07FF
    eid = (((producer_id & 0x60) >> 5) << 16) | (((consumer_id & 0x60) >> 5) << 14) | (cw | 0x80 if req_ack else (cw & 0x7F))
    return (sid << 18) | (eid & 0x3FFFF)

def decode_can_id(can_id: int):
    sid = (can_id >> 18) & 0x07FF
    eid = can_id & 0x3FFFF
    producer_id = ((eid >> 11) & 0x60) | ((sid >> 6) & 0x1F)
    cw = eid & 0xFF
    is_ack = ((cw & 0x80) == 0)
    return producer_id, cw, is_ack

def run_tests():
    print("============================================================")
    print("Testing UIM342 SimpleCAN 3.0 Codec Vectors")
    print("============================================================")

    # 1. Manual Page 23: Master ID 4 -> Motor ID 5, MO=1 (CW=0x15) with ACK -> CAN-ID: 0x04280095
    can_id_mo = encode_can_id(4, 5, 0x15, req_ack=True)
    assert can_id_mo == 0x04280095, f"Expected 0x04280095, got 0x{can_id_mo:08X}"
    print("[PASS] Manual Example 1: MO command CAN-ID = 0x04280095")

    # 2. Manual Page 24:
    # JV (CW=0x1D) with ACK -> 0x0428009D
    assert encode_can_id(4, 5, 0x1D, True) == 0x0428009D
    # BG (CW=0x16) with ACK -> 0x04280096
    assert encode_can_id(4, 5, 0x16, True) == 0x04280096
    # ST (CW=0x17) with ACK -> 0x04280097
    assert encode_can_id(4, 5, 0x17, True) == 0x04280097
    # PR (CW=0x1F) with ACK -> 0x0428009F
    assert encode_can_id(4, 5, 0x1F, True) == 0x0428009F
    # PA (CW=0x20) with ACK -> 0x042800A0
    assert encode_can_id(4, 5, 0x20, True) == 0x042800A0
    # SP (CW=0x1E) with ACK -> 0x0428009E
    assert encode_can_id(4, 5, 0x1E, True) == 0x0428009E
    print("[PASS] Manual Example 2: Motion commands CAN-IDs (JV, BG, ST, PR, PA, SP)")

    # 3. Test Decoding
    prod, cw, is_ack = decode_can_id(0x04280095)
    assert prod == 4
    assert cw == 0x95
    assert not is_ack

    prod_ack, cw_ack, is_ack_resp = decode_can_id(encode_can_id(5, 4, 0x15, False))
    assert prod_ack == 5
    assert cw_ack == 0x15
    assert is_ack_resp
    print("[PASS] CAN-ID Bidirectional Decode")

    # 4. Quick Feed (QF) 8-byte frame packing test (Manual Page 97)
    time_ms = 50 # 0x32
    vel = -1000  # 24-bit 0xFFFC18
    pos = 10000  # 32-bit 0x00002710
    
    vel_24 = vel & 0xFFFFFF
    pos_32 = pos & 0xFFFFFFFF
    
    payload = bytes([
        time_ms,
        vel_24 & 0xFF, (vel_24 >> 8) & 0xFF, (vel_24 >> 16) & 0xFF,
        pos_32 & 0xFF, (pos_32 >> 8) & 0xFF, (pos_32 >> 16) & 0xFF, (pos_32 >> 24) & 0xFF
    ])
    
    expected_payload = bytes([0x32, 0x18, 0xFC, 0xFF, 0x10, 0x27, 0x00, 0x00])
    assert payload == expected_payload, f"Expected {expected_payload.hex()}, got {payload.hex()}"
    print(f"[PASS] Manual Example 3: QF Frame Payload = {payload.hex()}")

    print("============================================================")
    print("ALL 4 PROTOCOL TEST SUITES PASSED PERFECTLY!")
    print("============================================================")

if __name__ == "__main__":
    run_tests()
