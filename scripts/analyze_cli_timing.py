#!/usr/bin/env python3
"""
analyze_cli_timing.py - Wire Timing Analysis for EmbeddedPixel CLI (Port 50002)

Analyzes packet captures (.pcap, .pcapng, .etl) to locate latency bottlenecks
in the 14-step EmbeddedPixel CLI execution signal path.
"""

import sys
import os
import json
import subprocess
import argparse
from pathlib import Path

TSHARK_PATHS = [
    r"C:\Program Files\Wireshark\tshark.exe",
    r"C:\Program Files (x86)\Wireshark\tshark.exe",
    "tshark"
]

def find_tshark():
    for p in TSHARK_PATHS:
        try:
            res = subprocess.run([p, "-v"], capture_output=True, text=True)
            if res.returncode == 0:
                return p
        except Exception:
            continue
    return None

def run_tshark_extraction(tshark_bin, capture_file, target_ip="169.254.127.150", target_port=50002):
    display_filter = f"tcp.port == {target_port} && ip.addr == {target_ip}"
    fields = [
        "-e", "frame.number",
        "-e", "frame.time_epoch",
        "-e", "frame.time_relative",
        "-e", "frame.time_delta_displayed",
        "-e", "ip.src",
        "-e", "ip.dst",
        "-e", "tcp.srcport",
        "-e", "tcp.dstport",
        "-e", "tcp.flags",
        "-e", "tcp.flags.syn",
        "-e", "tcp.flags.ack",
        "-e", "tcp.flags.push",
        "-e", "tcp.flags.fin",
        "-e", "tcp.flags.reset",
        "-e", "tcp.flags.str",
        "-e", "tcp.len",
        "-e", "tcp.stream",
        "-e", "tcp.analysis.retransmission",
        "-e", "_ws.col.Info"
    ]

    cmd = [
        tshark_bin,
        "-r", str(capture_file),
        "-Y", display_filter,
        "-T", "fields",
        "-E", "separator=\t",
        "-E", "header=y",
        "-E", "quote=d"
    ] + fields

    res = subprocess.run(cmd, capture_output=True, text=True)
    if res.returncode != 0:
        print(f"Error running tshark: {res.stderr}", file=sys.stderr)
        return []

    lines = res.stdout.strip().splitlines()
    if not lines or len(lines) < 2:
        return []

    headers = [h.strip('"') for h in lines[0].split("\t")]
    packets = []
    for line in lines[1:]:
        parts = [p.strip('"') for p in line.split("\t")]
        if len(parts) == len(headers):
            pkg = dict(zip(headers, parts))
            packets.append(pkg)
    return packets

def analyze_packets(packets, target_ip="169.254.127.150"):
    if not packets:
        print("No matching TCP port 50002 packets found in capture.")
        return

    print("=" * 90)
    print(f"  EMBEDDEDPIXEL TCP WIRE TIMING ANALYSIS (Target: {target_ip}:50002)")
    print("=" * 90)
    print(f"{'#':<4} {'Time (s)':<10} {'Delta (ms)':<11} {'Source':<16} {'Destination':<16} {'Flags':<8} {'Len':<5} {'Info'}")
    print("-" * 90)

    for p in packets:
        f_num = p.get("frame.number", "")
        t_rel = float(p.get("frame.time_relative", 0.0))
        t_delta = float(p.get("frame.time_delta_displayed", 0.0)) * 1000.0
        src = p.get("ip.src", "")
        dst = p.get("ip.dst", "")
        flags_str = p.get("tcp.flags.str", "")
        t_len = p.get("tcp.len", "0")
        info = p.get("_ws.col.Info", "")
        print(f"{f_num:<4} {t_rel:<10.6f} {t_delta:<11.3f} {src:<16} {dst:<16} {flags_str:<8} {t_len:<5} {info}")

    print("-" * 90)

    # State machine to detect transactions
    # 1. Host SYN
    # 2. Firmware SYN-ACK
    # 3. Host ACK
    # 4. Host PSH (CMD_CLI_EXEC data)
    # 5. Firmware ACK
    # 6. Firmware PSH (CMD_CLI_EXEC_RESP)
    # 7. Host ACK / FIN

    syn_pkt = None
    syn_ack_pkt = None
    client_psh_pkt = None
    fw_ack_pkt = None
    fw_psh_pkt = None

    for p in packets:
        src = p.get("ip.src", "")
        dst = p.get("ip.dst", "")
        syn = p.get("tcp.flags.syn", "0") == "1"
        ack = p.get("tcp.flags.ack", "0") == "1"
        psh = p.get("tcp.flags.push", "0") == "1"
        t_len = int(p.get("tcp.len", "0") or "0")

        # Client SYN
        if syn and not ack and dst == target_ip and syn_pkt is None:
            syn_pkt = p

        # Firmware SYN-ACK
        if syn and ack and src == target_ip and syn_ack_pkt is None:
            syn_ack_pkt = p

        # Client PSH (Data send)
        if psh and dst == target_ip and t_len > 0 and client_psh_pkt is None:
            client_psh_pkt = p

        # Firmware ACK after client data
        if ack and not psh and src == target_ip and client_psh_pkt is not None and fw_ack_pkt is None:
            fw_ack_pkt = p

        # Firmware PSH (Data response)
        if psh and src == target_ip and t_len > 0 and fw_psh_pkt is None:
            fw_psh_pkt = p

    print("\n" + "=" * 90)
    print("  EXACT TIMING DELTA BREAKDOWN")
    print("=" * 90)

    # Calculate metrics
    d_syn_to_synack = None
    d_psh_to_fw_ack = None
    d_fw_ack_to_fw_psh = None
    d_total = None

    if syn_pkt and syn_ack_pkt:
        t1 = float(syn_pkt["frame.time_epoch"])
        t2 = float(syn_ack_pkt["frame.time_epoch"])
        d_syn_to_synack = (t2 - t1) * 1000.0
        print(f"1. TCP Handshake Accept (SYN -> SYN-ACK):        {d_syn_to_synack:10.3f} ms")
    else:
        print("1. TCP Handshake Accept (SYN -> SYN-ACK):        [Handshake not captured / already open]")

    if client_psh_pkt and fw_ack_pkt:
        t3 = float(client_psh_pkt["frame.time_epoch"])
        t4 = float(fw_ack_pkt["frame.time_epoch"])
        d_psh_to_fw_ack = (t4 - t3) * 1000.0
        print(f"2. TCP Recv / ACK Latency (Client PSH -> FW ACK): {d_psh_to_fw_ack:10.3f} ms")
    elif client_psh_pkt and fw_psh_pkt and not fw_ack_pkt:
        # Piggybacked ACK on response
        t3 = float(client_psh_pkt["frame.time_epoch"])
        t5 = float(fw_psh_pkt["frame.time_epoch"])
        d_psh_to_fw_ack = (t5 - t3) * 1000.0
        print(f"2. TCP Recv / ACK Latency (Piggybacked on PSH):   {d_psh_to_fw_ack:10.3f} ms")
    else:
        print("2. TCP Recv / ACK Latency:                       [Data packet not captured]")

    if (fw_ack_pkt or client_psh_pkt) and fw_psh_pkt:
        base_t = float(fw_ack_pkt["frame.time_epoch"]) if fw_ack_pkt else float(client_psh_pkt["frame.time_epoch"])
        t5 = float(fw_psh_pkt["frame.time_epoch"])
        d_fw_ack_to_fw_psh = (t5 - base_t) * 1000.0
        print(f"3. CLI Command Processing (FW ACK -> FW PSH):    {d_fw_ack_to_fw_psh:10.3f} ms")
    else:
        print("3. CLI Command Processing:                       [Response packet not captured]")

    if syn_pkt and fw_psh_pkt:
        t_start = float(syn_pkt["frame.time_epoch"])
        t_end = float(fw_psh_pkt["frame.time_epoch"])
        d_total = (t_end - t_start) * 1000.0
        print(f"4. Total End-to-End Wire Latency:                {d_total:10.3f} ms")
    elif client_psh_pkt and fw_psh_pkt:
        t_start = float(client_psh_pkt["frame.time_epoch"])
        t_end = float(fw_psh_pkt["frame.time_epoch"])
        d_total = (t_end - t_start) * 1000.0
        print(f"4. Total Data Round-Trip Latency:                {d_total:10.3f} ms")

    print("=" * 90)
    print("  DIAGNOSTIC VERDICT")
    print("=" * 90)

    # Verdict classification
    retransmissions = [p for p in packets if p.get("tcp.analysis.retransmission") == "1"]
    if retransmissions:
        print(f"[!] WARNING: Detected {len(retransmissions)} TCP Retransmissions on the wire.")

    if d_syn_to_synack is not None and d_syn_to_synack > 1500.0:
        print("[*] MATCH: >> SCENARIO A: SLOW LWIP POLLING / TICK SCHEDULING <<")
        print(f"    - SYN -> SYN-ACK took {d_syn_to_synack:.1f}ms (~3s gap in handshake).")
        print("    - Root Cause: MX_LWIP_Process() or Ethernet DMA poll interval is ~3000ms.")
        print("    - Fix: Ensure MX_LWIP_Process() or sys_check_timeouts() is called frequently in FreeRTOS task.")

    elif d_psh_to_fw_ack is not None and 200.0 <= d_psh_to_fw_ack <= 600.0:
        print("[*] MATCH: >> SCENARIO B: LWIP DELAYED ACK <<")
        print(f"    - Client PSH -> FW ACK took {d_psh_to_fw_ack:.1f}ms (~500ms delayed ACK timer).")
        print("    - Root Cause: TCP_ACK_DELAY = 500ms in lwipopts.h.")
        print("    - Fix: Set #define TCP_ACK_DELAY 0 in lwipopts.h or use tcp_output() after receive.")

    elif d_fw_ack_to_fw_psh is not None and d_fw_ack_to_fw_psh > 1500.0:
        print("[*] MATCH: >> SCENARIO C: CLI TASK STARVATION / HANDLER BLOCKING <<")
        print(f"    - FW ACK -> FW Response PSH took {d_fw_ack_to_fw_psh:.1f}ms (~3s gap in execution).")
        print("    - Root Cause: CLI FreeRTOS task is starved, lower priority, or blocking on internal lock.")
        print("    - Fix: Elevate CLI task priority or inspect internal semaphore/mutex in CLI handler.")

    elif d_total is not None and d_total < 50.0:
        print("[*] MATCH: >> HEALTHY / HIGH SPEED <<")
        print(f"    - Total latency is only {d_total:.2f}ms. No wire-level latency issue detected.")

    else:
        print("[*] MATCH: >> CUSTOM / INTERMEDIATE DELAY <<")
        if d_syn_to_synack:
            print(f"    - SYN -> SYN-ACK: {d_syn_to_synack:.2f}ms")
        if d_psh_to_fw_ack:
            print(f"    - PSH -> ACK: {d_psh_to_fw_ack:.2f}ms")
        if d_fw_ack_to_fw_psh:
            print(f"    - ACK -> Response: {d_fw_ack_to_fw_psh:.2f}ms")
    print("=" * 90)

def main():
    parser = argparse.ArgumentParser(description="Analyze TCP Port 50002 Timing for EmbeddedPixel CLI")
    parser.add_argument("capture_file", help="Path to .pcap, .pcapng, or .etl capture file")
    parser.add_argument("--ip", default="169.254.127.150", help="Firmware target IP address (default: 169.254.127.150)")
    parser.add_argument("--port", type=int, default=50002, help="CLI TCP port (default: 50002)")
    args = parser.parse_args()

    cap_path = Path(args.capture_file)
    if not cap_path.exists():
        print(f"Error: Capture file not found: {cap_path}", file=sys.stderr)
        sys.exit(1)

    # Convert .etl to .pcapng if needed using pktmon
    if cap_path.suffix.lower() == ".etl":
        pcap_target = cap_path.with_suffix(".pcapng")
        print(f"Converting ETL trace to PCAPNG: {pcap_target}")
        subprocess.run(["pktmon", "etl2pcap", str(cap_path), "-o", str(pcap_target)], check=False)
        if pcap_target.exists():
            cap_path = pcap_target

    tshark = find_tshark()
    if not tshark:
        print("Error: tshark.exe not found. Please ensure Wireshark is installed.", file=sys.stderr)
        sys.exit(1)

    packets = run_tshark_extraction(tshark, cap_path, target_ip=args.ip, target_port=args.port)
    analyze_packets(packets, target_ip=args.ip)

if __name__ == "__main__":
    main()
