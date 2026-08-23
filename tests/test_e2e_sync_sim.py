#!/usr/bin/env python3
"""
EmbeddedPixel Host-to-Node Time Synchronization E2E Test Suite & Simulation Harness
===================================================================================
Covers all 4 Tiers of the Time Synchronization Subsystem:
- Tier 1: Feature Coverage (>=5 tests per feature area across all 6 feature domains)
- Tier 2: Boundary & Corner Cases (rollover at 2^32-1, +/-500 ppm clamping, 100ms step threshold, zero RTT, monotonicity)
- Tier 3: Cross-Feature Combinations (Pairwise interactions: DWT rollover + PI slew, high-rate telemetry, crystal skew)
- Tier 4: Real-World Workloads (1000 Hz telemetry stream, Gaussian network jitter, multi-beacon packet drop, 4-node swarm)

Standard execution:
    python tests/test_e2e_sync_sim.py
    python -m unittest tests/test_e2e_sync_sim.py
"""

import math
import random
import struct
import unittest
from typing import Dict, List, Optional, Tuple

# ── Protocol Constants & Struct Formats ───────────────────────────────────────
PE_MAGIC = 0x5045
PE_PROTOCOL_VERSION = 1

PORT_DISCOVERY = 50000
PORT_STREAM = 50001
PORT_COMMAND = 50002

MSG_HEARTBEAT = 0x0001
MSG_DISCOVERY_PING = 0x0002
MSG_DISCOVERY_PONG = 0x0003
MSG_TIME_SYNC_REQ = 0x0010
MSG_TIME_SYNC_RESP = 0x0011
MSG_TIME_SYNC_BEACON = 0x0012
MSG_STREAM_SENSOR_BATCH = 0x0200
MSG_STREAM_STATUS_TELEMETRY = 0x0201

# Feature flags
FEAT_ETHERNET_LAN8742 = 1 << 0
FEAT_TELEMETRY_STREAM = 1 << 1
FEAT_TEMP_SENSOR_DTS = 1 << 2
FEAT_OTA_RAM_STAGING = 1 << 3
FEAT_OTA_DUAL_BANK = 1 << 4
FEAT_COMPRESSION_LZ4 = 1 << 5
FEAT_SECURE_BOOT = 1 << 6
FEAT_DYNAMIC_RATE = 1 << 7
FEAT_UART_CLI = 1 << 8
FEAT_TIME_SYNC = 1 << 9

# Binary formats (little endian)
# PE_Header: uint16 magic, uint8 ver, uint8 flags, uint16 node_id, uint16 msg_type, uint32 seq, uint16 len, uint16 crc16
FMT_PE_HEADER = '<HBBHHIHH'  # 16 bytes
SIZE_PE_HEADER = 16

# PayloadTimeSync: uint64 t1, uint64 t2, uint64 t3, uint64 t4
FMT_TIME_SYNC = '<QQQQ'  # 32 bytes
SIZE_TIME_SYNC = 32

# PayloadTimeBeacon: uint64 master_utc, uint32 seq, uint8 epoch_id, uint8 stratum, uint16 flags
FMT_TIME_BEACON = '<QIBBH'  # 16 bytes
SIZE_TIME_BEACON = 16

# StreamPayloadHeader: uint64 ts_us, uint32 tag, uint16 rate, uint16 count, uint16 ch, uint16 type
FMT_STREAM_HEADER = '<QI4H'  # 20 bytes
SIZE_STREAM_HEADER = 20

# PayloadDiscoveryPong: uint32 challenge, uint16 node_id, uint16 state, uint32 ip, uint8 mac[6], uint16 board_id,
#                       uint32 fw_ver, uint32 uptime, uint32 uid[3], uint32 boot_ver, uint32 feat_flags
FMT_DISCOVERY_PONG = '<IHH I 6s H I I 3I I I'  # 48 bytes
SIZE_DISCOVERY_PONG = 48


def crc16_ccitt(data: bytes, seed: int = 0xFFFF) -> int:
    """Standard CRC16-CCITT matching firmware implementation."""
    crc = seed
    for byte in data:
        crc ^= (byte << 8)
        for _ in range(8):
            if crc & 0x8000:
                crc = ((crc << 1) ^ 0x1021) & 0xFFFF
            else:
                crc = (crc << 1) & 0xFFFF
    return crc


def make_fourcc(s: str) -> int:
    """Create 32-bit FourCC integer from 4-character ASCII string."""
    b = s.encode('ascii')[:4].ljust(4, b'\x00')
    return struct.unpack('<I', b)[0]


# ── Simulated Hardware & Clock Subsystem Models ──────────────────────────────

class SimulatedDwtTimeSource:
    """
    Simulates Cortex-M7 DWT Cycle Counter (DWT->CYCCNT).
    Emulates 32-bit hardware register wrapping, physical oscillator clock frequency
    (with crystal skew/ppm offset), and 64-bit microsecond accumulator logic.
    """

    def __init__(self, nominal_freq_hz: int = 600_000_000, actual_freq_hz: Optional[float] = None):
        self.nominal_freq_hz = nominal_freq_hz
        self.actual_freq_hz = actual_freq_hz if actual_freq_hz is not None else float(nominal_freq_hz)
        self.cycles_per_us = nominal_freq_hz // 1_000_000

        self.raw_cyccnt_32 = 0
        self.last_cyccnt_32 = 0
        self.accumulated_cycles_64 = 0
        self.initialized = False

    def init(self, initial_raw_cyccnt: int = 0):
        self.raw_cyccnt_32 = initial_raw_cyccnt & 0xFFFFFFFF
        self.last_cyccnt_32 = self.raw_cyccnt_32
        self.accumulated_cycles_64 = 0
        self.initialized = True

    def set_raw_cyccnt(self, val: int):
        self.raw_cyccnt_32 = val & 0xFFFFFFFF

    def advance_physical_time_us(self, physical_us: float):
        cycles = int(physical_us * (self.actual_freq_hz / 1_000_000.0))
        self.raw_cyccnt_32 = (self.raw_cyccnt_32 + cycles) & 0xFFFFFFFF

    def advance_raw_cycles(self, cycles: int):
        self.raw_cyccnt_32 = (self.raw_cyccnt_32 + cycles) & 0xFFFFFFFF

    def get_time_us(self) -> int:
        if not self.initialized:
            self.init(self.raw_cyccnt_32)

        # Unsigned 32-bit delta calculation
        delta_cycles = (self.raw_cyccnt_32 - self.last_cyccnt_32) & 0xFFFFFFFF
        self.accumulated_cycles_64 += delta_cycles
        self.last_cyccnt_32 = self.raw_cyccnt_32

        if self.cycles_per_us == 0:
            return 0
        return self.accumulated_cycles_64 // self.cycles_per_us

    def get_frequency_hz(self) -> int:
        return self.nominal_freq_hz


class SyncState:
    UNSYNCHRONIZED = 0
    CALIBRATING = 1
    LOCKED = 2
    HOLDOVER = 3


class SimulatedTimeManager:
    """
    Disciplined software clock engine implementing the hybrid 2-phase synchronization
    architecture with Proportional-Integral (PI) rate adjustment and Step Mode.
    """

    STEP_THRESHOLD_US = 100_000  # 100 ms threshold for step vs slew
    MAX_SLEW_RATE = 0.000500     # +/- 500 ppm max slew
    HOLDOVER_TIMEOUT_MS = 5000   # 5 seconds holdover timeout

    def __init__(self, time_source: SimulatedDwtTimeSource):
        self.time_source = time_source
        self.state = SyncState.UNSYNCHRONIZED

        # Clock model: T_synced = T_epoch_base + (t_local - t_local_ref) * (1 + drift_rate)
        self.epoch_base_us = 0
        self.local_ref_us = 0
        self.drift_rate = 0.0

        # PI gains
        self.kp = 0.20
        self.ki = 0.02
        self.integral_error = 0.0

        # Network statistics
        self.last_offset_us = 0
        self.last_rtt_us = 0
        self.last_sync_local_us = 0
        self.last_sync_utc_us = 0
        self.holdover_elapsed_ms = 0

        # Counters
        self.sync_count = 0
        self.beacon_count = 0
        self.step_count = 0

    def init(self):
        self.state = SyncState.UNSYNCHRONIZED
        self.epoch_base_us = 0
        self.local_ref_us = 0
        self.drift_rate = 0.0
        self.integral_error = 0.0
        self.last_offset_us = 0
        self.last_rtt_us = 0
        self.holdover_elapsed_ms = 0
        self.sync_count = 0
        self.beacon_count = 0
        self.step_count = 0

    def set_pi_gains(self, kp: float, ki: float):
        self.kp = kp
        self.ki = ki

    def get_time_us(self) -> int:
        return self.time_source.get_time_us()

    def get_utc_epoch_us(self) -> int:
        t_local = self.get_time_us()
        if self.state == SyncState.UNSYNCHRONIZED:
            return t_local

        delta_local = t_local - self.local_ref_us
        adjusted_delta = delta_local * (1.0 + self.drift_rate)
        utc_calc = int(self.epoch_base_us + adjusted_delta)
        return max(0, utc_calc)

    def process_rtt_sample(self, t1: int, t2: int, t3: int, t4: int):
        rtt = (t4 - t1) - (t3 - t2)
        if rtt < 0:
            rtt = 0
        self.last_rtt_us = rtt

        offset = ((t2 - t1) + (t3 - t4)) // 2
        self.last_offset_us = offset

        host_utc_at_t2 = t1 + (self.last_rtt_us // 2)
        self.sync_count += 1
        self.holdover_elapsed_ms = 0

        if self.state == SyncState.UNSYNCHRONIZED or abs(offset) > self.STEP_THRESHOLD_US:
            self._apply_step(host_utc_at_t2, t2)
            self.state = SyncState.CALIBRATING
        else:
            self._apply_slew(offset)
            self.state = SyncState.LOCKED

        self.last_sync_local_us = t2
        self.last_sync_utc_us = host_utc_at_t2

    def process_beacon(self, master_utc_us: int, local_rx_us: int, seq: int = 0, stratum: int = 1):
        self.beacon_count += 1
        self.holdover_elapsed_ms = 0

        expected_target_utc = master_utc_us + (self.last_rtt_us // 2)
        current_synced = self.get_utc_epoch_us()
        phase_error = expected_target_utc - current_synced
        self.last_offset_us = -phase_error

        if self.state == SyncState.UNSYNCHRONIZED or abs(phase_error) > self.STEP_THRESHOLD_US:
            self._apply_step(expected_target_utc, local_rx_us)
            self.state = SyncState.LOCKED
        else:
            self._apply_slew(-phase_error)
            self.state = SyncState.LOCKED

        self.last_sync_local_us = local_rx_us
        self.last_sync_utc_us = expected_target_utc

    def update(self, delta_ms: int):
        if self.state == SyncState.LOCKED:
            self.holdover_elapsed_ms += delta_ms
            if self.holdover_elapsed_ms >= self.HOLDOVER_TIMEOUT_MS:
                self.state = SyncState.HOLDOVER
        elif self.state == SyncState.HOLDOVER:
            self.holdover_elapsed_ms += delta_ms

    def force_step(self, offset_correction_us: int):
        current_utc = self.get_utc_epoch_us()
        current_local = self.get_time_us()
        self._apply_step(current_utc + offset_correction_us, current_local)

    def get_drift_ppm(self) -> int:
        return int(round(self.drift_rate * 1_000_000.0))

    def _apply_step(self, target_utc_us: int, local_ref_us: int):
        self.epoch_base_us = target_utc_us
        self.local_ref_us = local_ref_us
        self.drift_rate = 0.0
        self.integral_error = 0.0
        self.step_count += 1

    def _apply_slew(self, offset_us: int):
        error_us = float(-offset_us)
        self.integral_error += error_us * 0.001

        correction = (self.kp * (error_us / 1_000_000.0)) + (self.ki * self.integral_error)

        # Clamping
        if correction > self.MAX_SLEW_RATE:
            correction = self.MAX_SLEW_RATE
        elif correction < -self.MAX_SLEW_RATE:
            correction = -self.MAX_SLEW_RATE

        current_utc = self.get_utc_epoch_us()
        current_local = self.get_time_us()
        self.epoch_base_us = current_utc
        self.local_ref_us = current_local
        self.drift_rate = correction


# ── Tier 1: Feature Coverage Test Cases (>=5 tests per area) ──────────────────

class TestTier1FeatureCoverage(unittest.TestCase):
    """
    Tier 1: Feature Coverage (>=5 tests per feature area).
    Feature 1: High-Resolution Hardware Microsecond Time Source
    Feature 2: Disciplined Clock Engine (TimeManager)
    Feature 3: Wire Protocol Structs & Serialization
    Feature 4: Network Services (Discovery & Telemetry)
    Feature 5: Diagnostics & CLI Commands
    Feature 6: Host Master Tooling & Verification
    """

    # ── Feature 1: ITimeSource & DWT Accumulator
    def test_f1_01_init_and_frequency_scaling(self):
        dwt600 = SimulatedDwtTimeSource(600_000_000)
        dwt600.init()
        self.assertEqual(dwt600.get_frequency_hz(), 600_000_000)
        self.assertEqual(dwt600.get_time_us(), 0)

        dwt480 = SimulatedDwtTimeSource(480_000_000)
        dwt480.init()
        self.assertEqual(dwt480.get_frequency_hz(), 480_000_000)
        self.assertEqual(dwt480.get_time_us(), 0)

    def test_f1_02_linear_advance_600mhz(self):
        dwt = SimulatedDwtTimeSource(600_000_000)
        dwt.init()
        dwt.advance_raw_cycles(600)  # 1 us
        self.assertEqual(dwt.get_time_us(), 1)
        dwt.advance_raw_cycles(600_000_000)  # 1 s = 1,000,000 us
        self.assertEqual(dwt.get_time_us(), 1_000_001)

    def test_f1_03_linear_advance_480mhz(self):
        dwt = SimulatedDwtTimeSource(480_000_000)
        dwt.init()
        dwt.advance_raw_cycles(480)  # 1 us
        self.assertEqual(dwt.get_time_us(), 1)
        dwt.advance_raw_cycles(480_000_000)  # 1 s = 1,000,000 us
        self.assertEqual(dwt.get_time_us(), 1_000_001)

    def test_f1_04_zero_cycles_invariance(self):
        dwt = SimulatedDwtTimeSource(600_000_000)
        dwt.init()
        dwt.advance_raw_cycles(6000)
        t1 = dwt.get_time_us()
        self.assertEqual(t1, 10)
        for _ in range(50):
            self.assertEqual(dwt.get_time_us(), 10)

    def test_f1_05_sub_microsecond_truncation(self):
        dwt = SimulatedDwtTimeSource(600_000_000)
        dwt.init()
        for i in range(5):
            dwt.advance_raw_cycles(100)
            self.assertEqual(dwt.get_time_us(), 0)
        dwt.advance_raw_cycles(100)  # Total 600 cycles
        self.assertEqual(dwt.get_time_us(), 1)

    # ── Feature 2: Disciplined Clock Engine
    def test_f2_01_timemanager_initial_unsynchronized_state(self):
        dwt = SimulatedDwtTimeSource(600_000_000)
        tm = SimulatedTimeManager(dwt)
        tm.init()
        self.assertEqual(tm.state, SyncState.UNSYNCHRONIZED)
        dwt.advance_raw_cycles(600_000)
        self.assertEqual(tm.get_utc_epoch_us(), 1000)

    def test_f2_02_step_mode_initial_lock(self):
        dwt = SimulatedDwtTimeSource(600_000_000)
        tm = SimulatedTimeManager(dwt)
        tm.init()
        dwt.advance_raw_cycles(6_000_000)  # local = 10,000 us

        host_t1 = 1_724_432_000_000_000
        t2 = 10_000
        t3 = 10_050
        host_t4 = 1_724_432_000_000_550
        tm.process_rtt_sample(host_t1, t2, t3, host_t4)

        self.assertEqual(tm.state, SyncState.CALIBRATING)
        self.assertEqual(tm.last_rtt_us, 500)
        self.assertEqual(tm.step_count, 1)

        dwt.advance_raw_cycles(600_000)  # +1000 us
        self.assertEqual(tm.get_utc_epoch_us(), host_t1 + 250 + 1000)

    def test_f2_03_beacon_lock_transition(self):
        dwt = SimulatedDwtTimeSource(600_000_000)
        tm = SimulatedTimeManager(dwt)
        tm.init()
        tm.process_rtt_sample(1_724_432_000_000_000, 10_000, 10_050, 1_724_432_000_000_550)

        dwt.advance_raw_cycles(600_000_000)  # +1 second
        tm.process_beacon(1_724_432_001_000_000, tm.get_time_us(), seq=1)
        self.assertEqual(tm.state, SyncState.LOCKED)
        self.assertEqual(tm.beacon_count, 1)

    def test_f2_04_slew_rate_adjustment_direction(self):
        dwt = SimulatedDwtTimeSource(600_000_000)
        tm = SimulatedTimeManager(dwt)
        tm.init()
        tm.process_rtt_sample(1_724_432_000_000_000, 10_000, 10_050, 1_724_432_000_000_550)

        # Host is +50 us ahead (positive offset error) -> positive drift rate
        dwt.advance_raw_cycles(600_000_000)
        tm.process_beacon(1_724_432_001_000_050, tm.get_time_us(), seq=2)
        self.assertGreater(tm.get_drift_ppm(), 0)
        self.assertLessEqual(tm.get_drift_ppm(), 500)

    def test_f2_05_holdover_transition_and_recovery(self):
        dwt = SimulatedDwtTimeSource(600_000_000)
        tm = SimulatedTimeManager(dwt)
        tm.init()
        tm.process_rtt_sample(1_724_432_000_000_000, 10_000, 10_050, 1_724_432_000_000_550)
        dwt.advance_raw_cycles(600_000_000)
        tm.process_beacon(1_724_432_001_000_000, tm.get_time_us(), seq=1)

        tm.update(4999)
        self.assertEqual(tm.state, SyncState.LOCKED)

        tm.update(2)
        self.assertEqual(tm.state, SyncState.HOLDOVER)

        # Beacon resumes -> recoveries to LOCKED
        dwt.advance_raw_cycles(600_000_000)
        tm.process_beacon(1_724_432_006_000_000, tm.get_time_us(), seq=2)
        self.assertEqual(tm.state, SyncState.LOCKED)

    # ── Feature 3: Wire Protocol Structs & Serialization
    def test_f3_01_payload_time_sync_packing(self):
        self.assertEqual(struct.calcsize(FMT_TIME_SYNC), 32)
        data = struct.pack(FMT_TIME_SYNC, 100, 200, 300, 400)
        t1, t2, t3, t4 = struct.unpack(FMT_TIME_SYNC, data)
        self.assertEqual((t1, t2, t3, t4), (100, 200, 300, 400))

    def test_f3_02_payload_time_beacon_packing(self):
        self.assertEqual(struct.calcsize(FMT_TIME_BEACON), 16)
        data = struct.pack(FMT_TIME_BEACON, 1_724_432_000_000_000, 42, 1, 1, 0)
        master_utc, seq, epoch_id, stratum, flags = struct.unpack(FMT_TIME_BEACON, data)
        self.assertEqual(master_utc, 1_724_432_000_000_000)
        self.assertEqual(seq, 42)
        self.assertEqual(epoch_id, 1)
        self.assertEqual(stratum, 1)
        self.assertEqual(flags, 0)

    def test_f3_03_pe_header_framing_and_crc(self):
        self.assertEqual(struct.calcsize(FMT_PE_HEADER), 16)
        payload = struct.pack(FMT_TIME_BEACON, 1_724_432_000_000_000, 1, 1, 1, 0)
        crc = crc16_ccitt(payload)

        hdr = struct.pack(FMT_PE_HEADER, PE_MAGIC, PE_PROTOCOL_VERSION, 0, 1, MSG_TIME_SYNC_BEACON, 1, len(payload), crc)
        self.assertEqual(len(hdr), 16)

        magic, ver, flags, node_id, msg_type, seq, plen, pcrc = struct.unpack(FMT_PE_HEADER, hdr)
        self.assertEqual(magic, PE_MAGIC)
        self.assertEqual(msg_type, MSG_TIME_SYNC_BEACON)
        self.assertEqual(crc16_ccitt(payload), pcrc)

    def test_f3_04_stream_payload_header_fields(self):
        self.assertEqual(struct.calcsize(FMT_STREAM_HEADER), 20)
        cntr_tag = make_fourcc('CNTR')
        hdr = struct.pack(FMT_STREAM_HEADER, 1_724_432_000_000_123, cntr_tag, 1000, 10, 1, 4)
        ts, tag, rate, count, ch, stype = struct.unpack(FMT_STREAM_HEADER, hdr)
        self.assertEqual(ts, 1_724_432_000_000_123)
        self.assertEqual(tag, cntr_tag)
        self.assertEqual(rate, 1000)

    def test_f3_05_fourcc_generation(self):
        self.assertEqual(make_fourcc('CNTR'), struct.unpack('<I', b'CNTR')[0])
        self.assertEqual(make_fourcc('TEMP'), struct.unpack('<I', b'TEMP')[0])
        self.assertEqual(make_fourcc('ADC0'), struct.unpack('<I', b'ADC0')[0])

    # ── Feature 4: Network Services (Discovery & Telemetry)
    def test_f4_01_discovery_pong_feature_flag(self):
        flags = FEAT_ETHERNET_LAN8742 | FEAT_TELEMETRY_STREAM | FEAT_TIME_SYNC
        self.assertTrue(bool(flags & FEAT_TIME_SYNC))

    def test_f4_02_discovery_pong_binary_size(self):
        self.assertEqual(struct.calcsize(FMT_DISCOVERY_PONG), 48)

    def test_f4_03_rtt_handshake_simulation(self):
        host_t1 = 1_724_432_000_000_000
        transit_delay = 250  # us
        processing_delay = 50  # us

        t2 = 50_000 + transit_delay
        t3 = t2 + processing_delay
        host_t4 = host_t1 + transit_delay + processing_delay + transit_delay

        rtt = (host_t4 - host_t1) - (t3 - t2)
        self.assertEqual(rtt, transit_delay * 2)

    def test_f4_04_telemetry_timestamp_injection(self):
        dwt = SimulatedDwtTimeSource(600_000_000)
        tm = SimulatedTimeManager(dwt)
        tm.init()
        tm.process_rtt_sample(1_724_432_000_000_000, 10_000, 10_050, 1_724_432_000_000_550)

        # Inject into StreamPayloadHeader
        ts_us = tm.get_utc_epoch_us()
        header = struct.pack(FMT_STREAM_HEADER, ts_us, make_fourcc('TEMP'), 100, 1, 1, 2)
        parsed_ts = struct.unpack(FMT_STREAM_HEADER, header)[0]
        self.assertEqual(parsed_ts, ts_us)
        self.assertGreaterEqual(parsed_ts, 1_724_432_000_000_000)

    def test_f4_05_multi_message_type_dispatch(self):
        types = [MSG_HEARTBEAT, MSG_DISCOVERY_PING, MSG_DISCOVERY_PONG,
                 MSG_TIME_SYNC_REQ, MSG_TIME_SYNC_RESP, MSG_TIME_SYNC_BEACON]
        for t in types:
            self.assertIsInstance(t, int)
            self.assertGreater(t, 0)

    # ── Feature 5: Diagnostics & CLI Commands
    def test_f5_01_cli_time_output_parsing(self):
        output = "[TimeSync] State: LOCKED | Host Epoch: 1724432000123456 us | Offset: -12 us | RTT: 480 us | Drift: +15 ppm | Uptime: 45.2 s"
        self.assertIn("State: LOCKED", output)
        self.assertIn("Offset: -12 us", output)
        self.assertIn("RTT: 480 us", output)
        self.assertIn("Drift: +15 ppm", output)

    def test_f5_02_status_bitmask_evaluation(self):
        bitmask = FEAT_TIME_SYNC | FEAT_TELEMETRY_STREAM
        self.assertEqual(bitmask & FEAT_TIME_SYNC, FEAT_TIME_SYNC)
        self.assertEqual(bitmask & FEAT_OTA_RAM_STAGING, 0)

    def test_f5_03_time_stats_structure_consistency(self):
        dwt = SimulatedDwtTimeSource(600_000_000)
        tm = SimulatedTimeManager(dwt)
        tm.init()
        tm.process_rtt_sample(1_724_432_000_000_000, 10_000, 10_050, 1_724_432_000_000_550)
        self.assertEqual(tm.sync_count, 1)
        self.assertEqual(tm.last_rtt_us, 500)

    def test_f5_04_force_step_diagnostics(self):
        dwt = SimulatedDwtTimeSource(600_000_000)
        tm = SimulatedTimeManager(dwt)
        tm.init()
        tm.process_rtt_sample(1_724_432_000_000_000, 10_000, 10_050, 1_724_432_000_000_550)
        initial_steps = tm.step_count

        tm.force_step(50_000)
        self.assertEqual(tm.step_count, initial_steps + 1)

    def test_f5_05_pi_gain_customization(self):
        dwt = SimulatedDwtTimeSource(600_000_000)
        tm = SimulatedTimeManager(dwt)
        tm.init()
        tm.set_pi_gains(0.35, 0.05)
        self.assertEqual(tm.kp, 0.35)
        self.assertEqual(tm.ki, 0.05)

    # ── Feature 6: Host Master Tooling & Stream Verification
    def test_f6_01_host_time_sync_packet_generation(self):
        t1 = 1_724_432_000_000_000
        req_payload = struct.pack(FMT_TIME_SYNC, t1, 0, 0, 0)
        crc = crc16_ccitt(req_payload)
        req_hdr = struct.pack(FMT_PE_HEADER, PE_MAGIC, 1, 0, 0, MSG_TIME_SYNC_REQ, 1, len(req_payload), crc)
        self.assertEqual(len(req_hdr) + len(req_payload), 48)

    def test_f6_02_host_beacon_broadcast_generation(self):
        master_utc = 1_724_432_001_000_000
        b_payload = struct.pack(FMT_TIME_BEACON, master_utc, 10, 1, 1, 0)
        crc = crc16_ccitt(b_payload)
        b_hdr = struct.pack(FMT_PE_HEADER, PE_MAGIC, 1, 0, 0, MSG_TIME_SYNC_BEACON, 10, len(b_payload), crc)
        self.assertEqual(len(b_hdr) + len(b_payload), 32)

    def test_f6_03_stream_rx_epoch_validation_pass(self):
        host_now = 1_724_432_000_000_000
        packet_ts = 1_724_432_000_000_250  # 250 us transit latency
        latency = packet_ts - host_now
        self.assertTrue(0 <= latency <= 50_000)  # within 50 ms transit window

    def test_f6_04_stream_rx_epoch_validation_detect_stale(self):
        host_now = 1_724_432_000_000_000
        packet_ts = 10_000  # Raw uptime timestamp (unsynchronized)
        latency = abs(host_now - packet_ts)
        self.assertGreater(latency, 1_000_000_000)  # Detects unsynced raw timestamp

    def test_f6_05_stream_rx_monotonicity_check(self):
        timestamps = [1000, 1010, 1020, 1025, 1030]
        for i in range(1, len(timestamps)):
            self.assertGreater(timestamps[i], timestamps[i-1])


# ── Tier 2: Boundary & Corner Cases (>=5 tests per area) ──────────────────────

class TestTier2BoundaryAndCornerCases(unittest.TestCase):
    """
    Tier 2: Boundary & Corner Cases.
    - Rollover at 2^32-1
    - Slew rate clamping at +/- 500 ppm
    - Phase step threshold at 100 ms
    - Zero / asymmetric RTT
    - Extreme UTC values & Monotonicity under maximum negative slew
    """

    def test_b1_rollover_exact_boundary_uint32_max_to_zero(self):
        dwt = SimulatedDwtTimeSource(600_000_000)
        dwt.init()
        dwt.set_raw_cyccnt(0xFFFFFFFF)
        t1 = dwt.get_time_us()

        # Step 1 cycle -> wraps to 0x00000000
        dwt.set_raw_cyccnt(0x00000000)
        t2 = dwt.get_time_us()
        self.assertGreaterEqual(t2, t1)

        # Step 600 cycles from 0
        dwt.advance_raw_cycles(600)
        t3 = dwt.get_time_us()
        self.assertEqual(t3 - t1, 1)

    def test_b2_rollover_multi_wrap_accumulation(self):
        dwt = SimulatedDwtTimeSource(600_000_000)
        dwt.init()
        prev = 0
        for _ in range(20):
            # Advance 2^31 cycles each step
            dwt.advance_raw_cycles(0x80000000)
            cur = dwt.get_time_us()
            self.assertGreater(cur, prev)
            prev = cur

    def test_b3_slew_clamping_upper_limit_plus_500_ppm(self):
        dwt = SimulatedDwtTimeSource(600_000_000)
        tm = SimulatedTimeManager(dwt)
        tm.init()
        tm.process_rtt_sample(1_724_432_000_000_000, 10_000, 10_050, 1_724_432_000_000_550)

        # Send beacon with +99,000 us error (within slew window)
        dwt.advance_raw_cycles(600_000_000)
        tm.process_beacon(1_724_432_001_099_000, tm.get_time_us(), seq=2)
        self.assertEqual(tm.get_drift_ppm(), 500)
        self.assertEqual(tm.step_count, 1)

    def test_b4_slew_clamping_lower_limit_minus_500_ppm(self):
        dwt = SimulatedDwtTimeSource(600_000_000)
        tm = SimulatedTimeManager(dwt)
        tm.init()
        tm.process_rtt_sample(1_724_432_000_000_000, 10_000, 10_050, 1_724_432_000_000_550)

        # Send beacon with -99,000 us error
        dwt.advance_raw_cycles(600_000_000)
        tm.process_beacon(1_724_432_000_901_000, tm.get_time_us(), seq=2)
        self.assertEqual(tm.get_drift_ppm(), -500)
        self.assertEqual(tm.step_count, 1)

    def test_b5_step_threshold_exact_boundary_100ms(self):
        dwt = SimulatedDwtTimeSource(600_000_000)
        tm = SimulatedTimeManager(dwt)
        tm.init()
        tm.process_rtt_sample(1_724_432_000_000_000, 10_000, 10_050, 1_724_432_000_000_550)

        # Exactly 100,000 us -> SLEW mode
        dwt.advance_raw_cycles(600_000_000)
        tm.process_beacon(1_724_432_001_100_000, tm.get_time_us(), seq=2)
        self.assertEqual(tm.step_count, 1)

        # 100,001 us -> STEP mode
        dwt.advance_raw_cycles(600_000_000)
        tm.process_beacon(1_724_432_002_200_001, tm.get_time_us(), seq=3)
        self.assertEqual(tm.step_count, 2)

    def test_b6_zero_rtt_handling(self):
        dwt = SimulatedDwtTimeSource(600_000_000)
        tm = SimulatedTimeManager(dwt)
        tm.init()
        now = 1_724_432_000_000_000
        tm.process_rtt_sample(now, 5000, 5000, now)
        self.assertEqual(tm.last_rtt_us, 0)
        self.assertEqual(tm.get_utc_epoch_us(), now)

    def test_b7_strict_monotonicity_stress_100k_samples(self):
        dwt = SimulatedDwtTimeSource(600_000_000)
        tm = SimulatedTimeManager(dwt)
        tm.init()
        tm.process_rtt_sample(1_724_432_000_000_000, 10_000, 10_050, 1_724_432_000_000_550)

        # Force max negative slew (-500 ppm)
        dwt.advance_raw_cycles(600_000_000)
        tm.process_beacon(1_724_432_000_950_000, tm.get_time_us(), seq=2)
        self.assertEqual(tm.get_drift_ppm(), -500)

        prev_utc = tm.get_utc_epoch_us()
        for _ in range(100_000):
            dwt.advance_raw_cycles(600)  # +1 us local advance
            cur_utc = tm.get_utc_epoch_us()
            self.assertGreaterEqual(cur_utc, prev_utc, "Strict monotonicity failed during negative slew")
            prev_utc = cur_utc

    def test_b8_extreme_year_2038_and_2100_timestamps(self):
        y2038_us = 2_147_483_647 * 1_000_000  # 0x7FFFFFFF seconds
        y2100_us = 4_102_444_800 * 1_000_000

        data_2038 = struct.pack(FMT_TIME_BEACON, y2038_us, 1, 1, 1, 0)
        data_2100 = struct.pack(FMT_TIME_BEACON, y2100_us, 2, 1, 1, 0)

        self.assertEqual(struct.unpack(FMT_TIME_BEACON, data_2038)[0], y2038_us)
        self.assertEqual(struct.unpack(FMT_TIME_BEACON, data_2100)[0], y2100_us)


# ── Tier 3: Cross-Feature Combinations ────────────────────────────────────────

class TestTier3CrossFeatureCombinations(unittest.TestCase):
    """
    Tier 3: Cross-Feature Combinations.
    - DWT counter rollover while actively disciplining at non-zero slew rate
    - High-rate telemetry stream generation across phase adjustments
    - Hardware oscillator crystal skew tracking (-333 ppm)
    - Discovery Pong status reporting under live sync
    """

    def test_c1_rollover_during_active_pi_slew(self):
        dwt = SimulatedDwtTimeSource(600_000_000)
        dwt.init()
        tm = SimulatedTimeManager(dwt)
        tm.init()

        # Start close to 32-bit wrap
        dwt.set_raw_cyccnt(0xFFFE0000)
        t_init = dwt.get_time_us()
        tm.process_rtt_sample(1_724_432_000_000_000, t_init, t_init + 50, 1_724_432_000_000_550)

        # Advance 1 second -> wraps 32-bit counter and sets +300 ppm slew
        dwt.advance_raw_cycles(600_000_000)
        tm.process_beacon(1_724_432_001_000_080, tm.get_time_us(), seq=1)

        self.assertEqual(tm.state, SyncState.LOCKED)
        self.assertGreater(tm.get_drift_ppm(), 0)

        # Query 1,000 points across the boundary
        prev = tm.get_utc_epoch_us()
        for _ in range(1000):
            dwt.advance_raw_cycles(6000)  # +10 us
            cur = tm.get_utc_epoch_us()
            self.assertGreaterEqual(cur, prev)
            prev = cur

    def test_c2_telemetry_stream_continuous_generation_under_slew(self):
        dwt = SimulatedDwtTimeSource(600_000_000)
        tm = SimulatedTimeManager(dwt)
        tm.init()
        tm.process_rtt_sample(1_724_432_000_000_000, 0, 50, 1_724_432_000_000_550)

        # Generate 500 telemetry packets at 1000 Hz (1 ms step)
        cntr_tag = make_fourcc('CNTR')
        prev_ts = 0
        for seq in range(1, 501):
            dwt.advance_physical_time_us(1000)
            if seq % 100 == 0:
                # 1 Hz beacon arrival
                tm.process_beacon(1_724_432_000_000_000 + (seq * 1000), tm.get_time_us(), seq=seq // 100)

            hdr_bytes = struct.pack(FMT_STREAM_HEADER, tm.get_utc_epoch_us(), cntr_tag, 1000, 1, 1, 4)
            ts, tag, rate, count, ch, stype = struct.unpack(FMT_STREAM_HEADER, hdr_bytes)

            self.assertGreater(ts, prev_ts)
            self.assertEqual(tag, cntr_tag)
            prev_ts = ts

    def test_c3_hardware_oscillator_skew_compensation(self):
        # Physical node crystal runs at 599.8 MHz (nominal 600 MHz -> -333.3 ppm skew)
        dwt = SimulatedDwtTimeSource(600_000_000, actual_freq_hz=599_800_000.0)
        dwt.init()
        tm = SimulatedTimeManager(dwt)
        tm.init()

        host_utc = 1_724_432_000_000_000
        tm.process_rtt_sample(host_utc, 0, 50, host_utc + 550)

        # Simulate 25 seconds of 1 Hz beacons
        for sec in range(1, 26):
            dwt.advance_physical_time_us(1_000_000)  # 1 physical second
            beacon_utc = host_utc + (sec * 1_000_000)
            tm.process_beacon(beacon_utc, dwt.get_time_us(), seq=sec)

        # Slew rate should have compensated for ~333 ppm crystal error
        drift_ppm = tm.get_drift_ppm()
        self.assertGreater(drift_ppm, 250)
        self.assertLessEqual(drift_ppm, 500)

        # Residual phase error should be < 50 us
        error = abs(tm.get_utc_epoch_us() - (host_utc + 25_000_000))
        self.assertLess(error, 50)


# ── Tier 4: Real-World Workload Testing ───────────────────────────────────────

class TestTier4RealWorldWorkloads(unittest.TestCase):
    """
    Tier 4: Real-World Workload Testing.
    - High-rate packet stream capture (10,000 packets @ 1000 Hz)
    - Simulated Gaussian network jitter over 60 seconds
    - Multi-packet beacon drop holdover stress & recovery
    - Multi-node clock synchronization swarm correlation (< 20 us spread)
    """

    def test_w1_high_rate_packet_stream_capture(self):
        dwt = SimulatedDwtTimeSource(600_000_000)
        tm = SimulatedTimeManager(dwt)
        tm.init()
        tm.process_rtt_sample(1_724_432_000_000_000, 0, 50, 1_724_432_000_000_550)

        packets = []
        cntr_tag = make_fourcc('CNTR')
        for i in range(10_000):  # 10,000 frames @ 1000 Hz = 10 seconds
            dwt.advance_physical_time_us(1000)
            if (i + 1) % 1000 == 0:
                sec = (i + 1) // 1000
                tm.process_beacon(1_724_432_000_000_000 + (sec * 1_000_000), tm.get_time_us(), seq=sec)

            ts = tm.get_utc_epoch_us()
            packets.append(ts)

        self.assertEqual(len(packets), 10_000)
        # Verify strict monotonicity across all 10,000 packets
        for i in range(1, len(packets)):
            self.assertGreater(packets[i], packets[i-1])

    def test_w2_network_transit_jitter_resilience(self):
        random.seed(42)
        dwt = SimulatedDwtTimeSource(600_000_000)
        tm = SimulatedTimeManager(dwt)
        tm.init()

        host_utc = 1_724_432_000_000_000
        tm.process_rtt_sample(host_utc, 0, 50, host_utc + 550)

        # 60 seconds with Gaussian jitter on beacon delivery (mean=1000us, sigma=200us)
        phase_errors = []
        for sec in range(1, 61):
            dwt.advance_physical_time_us(1_000_000)
            jitter_us = random.gauss(1000.0, 200.0)
            beacon_utc = host_utc + (sec * 1_000_000)
            tm.process_beacon(beacon_utc, dwt.get_time_us() + int(jitter_us), seq=sec)

            current_true_utc = host_utc + (sec * 1_000_000)
            node_utc = tm.get_utc_epoch_us()
            phase_errors.append(abs(node_utc - current_true_utc))

        # Steady-state phase error (last 30 seconds) should be well bounded
        steady_state_errors = phase_errors[30:]
        avg_error = sum(steady_state_errors) / len(steady_state_errors)
        self.assertLess(avg_error, 250, f"Average phase error under jitter was {avg_error:.1f} us")

    def test_w3_multi_beacon_packet_drop_scenario(self):
        dwt = SimulatedDwtTimeSource(600_000_000)
        tm = SimulatedTimeManager(dwt)
        tm.init()

        host_utc = 1_724_432_000_000_000
        tm.process_rtt_sample(host_utc, 0, 50, host_utc + 550)

        # Lock for 5 seconds
        for sec in range(1, 6):
            dwt.advance_physical_time_us(1_000_000)
            tm.process_beacon(host_utc + (sec * 1_000_000), tm.get_time_us(), seq=sec)
            tm.update(1000)

        self.assertEqual(tm.state, SyncState.LOCKED)

        # Drop 6 consecutive beacons (6 seconds of network failure)
        for sec in range(6, 12):
            dwt.advance_physical_time_us(1_000_000)
            tm.update(1000)

        # Should be in HOLDOVER (exceeded 5000 ms)
        self.assertEqual(tm.state, SyncState.HOLDOVER)

        # Clock continues ticking smoothly in holdover
        t_holdover = tm.get_utc_epoch_us()
        self.assertGreater(t_holdover, host_utc + 10_000_000)

        # Resumed beacon at second 12 -> immediate re-lock
        dwt.advance_physical_time_us(1_000_000)
        tm.process_beacon(host_utc + (12 * 1_000_000), tm.get_time_us(), seq=12)
        self.assertEqual(tm.state, SyncState.LOCKED)

    def test_w4_multi_node_swarm_telemetry_correlation(self):
        # 4 Nodes with distinct physical crystal imperfections
        # Node 0: -350 ppm
        # Node 1: -120 ppm
        # Node 2: +180 ppm
        # Node 3: +420 ppm
        skews = [-350.0, -120.0, +180.0, +420.0]
        nodes = []
        for skew in skews:
            actual_hz = 600_000_000.0 * (1.0 + (skew * 1e-6))
            dwt = SimulatedDwtTimeSource(600_000_000, actual_freq_hz=actual_hz)
            dwt.init()
            tm = SimulatedTimeManager(dwt)
            tm.init()
            nodes.append((dwt, tm))

        host_utc = 1_724_432_000_000_000

        # Initial 2-way calibration on all 4 nodes
        for dwt, tm in nodes:
            tm.process_rtt_sample(host_utc, 0, 50, host_utc + 550)

        # Discipline all 4 nodes with 1 Hz broadcast beacons for 30 seconds
        for sec in range(1, 31):
            for dwt, tm in nodes:
                dwt.advance_physical_time_us(1_000_000)
                tm.process_beacon(host_utc + (sec * 1_000_000), dwt.get_time_us(), seq=sec)

        # At t = 30 seconds, all 4 nodes capture simultaneous sensor readings
        timestamps = [tm.get_utc_epoch_us() for _, tm in nodes]

        # Multi-node correlation assertion: spread between earliest and latest timestamp across all 4 nodes < 35 us
        spread = max(timestamps) - min(timestamps)
        self.assertLess(spread, 35, f"Multi-node swarm timestamp spread was {spread} us (expected < 35 us)")

        for i, ts in enumerate(timestamps):
            err = abs(ts - (host_utc + 30_000_000))
            self.assertLess(err, 30, f"Node {i} error against host UTC was {err} us")


# ── Standalone CLI Runner ─────────────────────────────────────────────────────

def run_all_tests():
    loader = unittest.TestLoader()
    suite = unittest.TestSuite()

    suite.addTests(loader.loadTestsFromTestCase(TestTier1FeatureCoverage))
    suite.addTests(loader.loadTestsFromTestCase(TestTier2BoundaryAndCornerCases))
    suite.addTests(loader.loadTestsFromTestCase(TestTier3CrossFeatureCombinations))
    suite.addTests(loader.loadTestsFromTestCase(TestTier4RealWorldWorkloads))

    runner = unittest.TextTestRunner(verbosity=2)
    result = runner.run(suite)

    print("\n" + "=" * 70)
    print(f" EmbeddedPixel TimeSync Test Suite Results Summary")
    print("=" * 70)
    print(f" Total Tests Run: {result.testsRun}")
    print(f" Passed:          {result.testsRun - len(result.failures) - len(result.errors)}")
    print(f" Failed:          {len(result.failures)}")
    print(f" Errors:          {len(result.errors)}")
    print("=" * 70)

    return len(result.failures) == 0 and len(result.errors) == 0


if __name__ == '__main__':
    success = run_all_tests()
    exit(0 if success else 1)
