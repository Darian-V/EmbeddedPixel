#!/usr/bin/env python3
"""
EmbeddedPixel - Telemetry View & Preset Manager
================================================
Save, manage, apply, and monitor custom multi-channel telemetry dashboard views.

Usage:
    python scripts/telemetry_views.py list
    python scripts/telemetry_views.py show <view_name>
    python scripts/telemetry_views.py save <view_name> --streams CNTR:50 TEMP:10 --desc "My Custom View"
    python scripts/telemetry_views.py apply <view_name> --ip 192.168.1.111
    python scripts/telemetry_views.py stream <view_name> --ip 192.168.1.111
    python scripts/telemetry_views.py capture <view_name> --ip 192.168.1.111  (Capture live node state to view)
"""

import argparse
import csv
import datetime
import json
import os
import socket
import struct
import sys
import time
from dataclasses import asdict, dataclass, field
from pathlib import Path
from typing import Any, Dict, List, Optional

# Standard search paths for view definitions
DEFAULT_VIEW_DIRS = [
    Path(__file__).resolve().parent.parent / "configs" / "views",
    Path.home() / ".embeddedpixel" / "views",
]

PORT_STREAM_UDP = 50001
PORT_CTRL_TCP   = 50002
PE_MAGIC        = 0x5045
MSG_CMD_START_STREAM = 0x0110
MSG_CMD_STOP_STREAM  = 0x0111
MSG_CMD_GET_STREAMS  = 0x0120


@dataclass
class ChannelDisplay:
    title: str = ""
    units: str = ""
    color: str = "cyan"
    y_min: Optional[float] = None
    y_max: Optional[float] = None
    warn_threshold: Optional[float] = None
    buffer_size: int = 100


@dataclass
class StreamConfig:
    tag: str
    rate_hz: int
    enabled: bool = True
    display: ChannelDisplay = field(default_factory=ChannelDisplay)

    def to_dict(self) -> Dict[str, Any]:
        d = {
            "tag": self.tag.upper()[:4],
            "rate_hz": int(self.rate_hz),
            "enabled": bool(self.enabled),
            "display": {k: v for k, v in asdict(self.display).items() if v is not None}
        }
        return d

    @classmethod
    def from_dict(cls, data: Dict[str, Any]) -> "StreamConfig":
        disp_data = data.get("display", {})
        display = ChannelDisplay(
            title=disp_data.get("title", f"Channel {data.get('tag', '')}"),
            units=disp_data.get("units", ""),
            color=disp_data.get("color", "cyan"),
            y_min=disp_data.get("y_min"),
            y_max=disp_data.get("y_max"),
            warn_threshold=disp_data.get("warn_threshold"),
            buffer_size=disp_data.get("buffer_size", 100),
        )
        return cls(
            tag=data.get("tag", "UNKN").upper()[:4],
            rate_hz=int(data.get("rate_hz", 10)),
            enabled=bool(data.get("enabled", True)),
            display=display,
        )


@dataclass
class ViewConfig:
    name: str
    description: str = ""
    version: int = 1
    node_id: int = 1
    streams: List[StreamConfig] = field(default_factory=list)
    layout: Dict[str, Any] = field(default_factory=lambda: {"mode": "dashboard", "refresh_rate_ms": 100})
    export: Dict[str, Any] = field(default_factory=lambda: {"save_csv": False, "csv_prefix": "telemetry"})

    def to_dict(self) -> Dict[str, Any]:
        return {
            "name": self.name,
            "description": self.description,
            "version": self.version,
            "node_id": self.node_id,
            "streams": [s.to_dict() for s in self.streams],
            "layout": self.layout,
            "export": self.export,
        }

    @classmethod
    def from_dict(cls, data: Dict[str, Any]) -> "ViewConfig":
        streams = [StreamConfig.from_dict(s) for s in data.get("streams", [])]
        return cls(
            name=data.get("name", "unnamed_view"),
            description=data.get("description", ""),
            version=data.get("version", 1),
            node_id=data.get("node_id", 1),
            streams=streams,
            layout=data.get("layout", {"mode": "dashboard", "refresh_rate_ms": 100}),
            export=data.get("export", {"save_csv": False, "csv_prefix": "telemetry"}),
        )


class TelemetryViewManager:
    """Manages discoverability, serialization, and remote execution of telemetry views."""

    def __init__(self, search_dirs: Optional[List[Path]] = None):
        self.search_dirs = search_dirs or DEFAULT_VIEW_DIRS
        for d in self.search_dirs:
            d.mkdir(parents=True, exist_ok=True)

    def list_views(self) -> List[Dict[str, Any]]:
        """Returns a summary of all discovered saved views."""
        views = []
        seen = set()
        for directory in self.search_dirs:
            if not directory.exists():
                continue
            for json_file in sorted(directory.glob("*.json")):
                if json_file.name in seen:
                    continue
                seen.add(json_file.name)
                try:
                    with open(json_file, "r", encoding="utf-8") as f:
                        data = json.load(f)
                    views.append({
                        "name": data.get("name", json_file.stem),
                        "description": data.get("description", ""),
                        "stream_count": len(data.get("streams", [])),
                        "streams": [s.get("tag", "") for s in data.get("streams", [])],
                        "path": str(json_file),
                    })
                except Exception as e:
                    views.append({
                        "name": json_file.stem,
                        "description": f"[Error reading file: {e}]",
                        "stream_count": 0,
                        "streams": [],
                        "path": str(json_file),
                    })
        return views

    def find_view_path(self, name: str) -> Optional[Path]:
        """Resolves view name to file path."""
        # Direct file path check
        p = Path(name)
        if p.exists():
            return p

        # Check search dirs
        for d in self.search_dirs:
            candidate = d / f"{name}.json"
            if candidate.exists():
                return candidate
            candidate_raw = d / name
            if candidate_raw.exists():
                return candidate_raw
        return None

    def load_view(self, name: str) -> ViewConfig:
        """Loads a ViewConfig by name or path."""
        path = self.find_view_path(name)
        if not path:
            raise FileNotFoundError(f"View preset '{name}' not found in search paths: {self.search_dirs}")
        with open(path, "r", encoding="utf-8") as f:
            data = json.load(f)
        return ViewConfig.from_dict(data)

    def save_view(self, view: ViewConfig, target_path: Optional[Path] = None) -> Path:
        """Saves a ViewConfig to JSON."""
        if target_path is None:
            # Default to first search dir
            target_path = self.search_dirs[0] / f"{view.name}.json"
        target_path.parent.mkdir(parents=True, exist_ok=True)
        with open(target_path, "w", encoding="utf-8") as f:
            json.dump(view.to_dict(), f, indent=2)
        return target_path

    def apply_view(self, view: ViewConfig, node_ip: str, timeout: float = 3.0) -> bool:
        """Applies stream rate settings from view preset to the node over TCP 50002."""
        print(f"Connecting to node {node_ip}:{PORT_CTRL_TCP} to apply view '{view.name}'...")
        sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        sock.settimeout(timeout)
        try:
            sock.connect((node_ip, PORT_CTRL_TCP))
            seq = 1
            for stream in view.streams:
                tag_bytes = stream.tag.encode("ascii").ljust(4, b"\x00")[:4]
                tag_val = struct.unpack("<I", tag_bytes)[0]
                
                if stream.enabled and stream.rate_hz > 0:
                    msg_type = MSG_CMD_START_STREAM
                    rate = stream.rate_hz
                    action = f"Start {stream.tag} @ {rate} Hz"
                else:
                    msg_type = MSG_CMD_STOP_STREAM
                    rate = 0
                    action = f"Stop {stream.tag}"

                # PE_Header (16B): magic=0x5045, ver=1, flags=1, node_id=view.node_id, msg_type, seq, len=8, crc=0
                hdr = struct.pack("<HBBHH I H H", PE_MAGIC, 1, 1, view.node_id, msg_type, seq, 8, 0)
                # PayloadCommand (8B): cmd_id=0, param1=rate, param2=tag_val
                payload = struct.pack("<HHI", 0, rate, tag_val)
                sock.sendall(hdr + payload)

                resp = sock.recv(1024)
                print(f"  [+] Applied: {action} (ACK received)")
                seq += 1
            print(f"[SUCCESS] View '{view.name}' successfully applied to {node_ip}!")
            return True
        except Exception as e:
            print(f"[ERROR] Failed to apply view to {node_ip}: {e}")
            return False
        finally:
            sock.close()

    def capture_live_view(self, node_ip: str, view_name: str, timeout: float = 3.0) -> ViewConfig:
        """Queries node registered streams and captures current state into a new view preset."""
        print(f"Querying active streams from {node_ip}:{PORT_CTRL_TCP}...")
        sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        sock.settimeout(timeout)
        try:
            sock.connect((node_ip, PORT_CTRL_TCP))
            # CMD_GET_STREAMS (0x0120)
            hdr = struct.pack("<HBBHH I H H", PE_MAGIC, 1, 1, 1, MSG_CMD_GET_STREAMS, 1, 0, 0)
            sock.sendall(hdr)
            resp = sock.recv(2048)

            if len(resp) < 20:
                raise ValueError("Malformed response received from node")

            stream_count, = struct.unpack_from("<H", resp, 16)
            streams = []
            offset = 20
            for i in range(stream_count):
                if offset + 32 > len(resp):
                    break
                tag_raw, name_raw, rate_hz, batch, channels, sample_type, enabled = struct.unpack_from(
                    "<4s 16s HHHH B", resp, offset
                )
                tag_str = tag_raw.decode("ascii", errors="replace").strip("\x00")
                name_str = name_raw.decode("ascii", errors="replace").strip("\x00")
                
                units = "°C" if tag_str == "TEMP" else ("counts" if tag_str == "CNTR" else "mV")
                color = "yellow" if tag_str == "TEMP" else ("cyan" if tag_str == "CNTR" else "magenta")

                streams.append(StreamConfig(
                    tag=tag_str,
                    rate_hz=rate_hz,
                    enabled=bool(enabled),
                    display=ChannelDisplay(title=name_str or f"Channel {tag_str}", units=units, color=color)
                ))
                offset += 32

            view = ViewConfig(
                name=view_name,
                description=f"Auto-captured view from {node_ip} at {datetime.datetime.now().isoformat()}",
                streams=streams
            )
            return view
        finally:
            sock.close()


def run_stream_dashboard(view: ViewConfig, listen_ip: str = "0.0.0.0", port: int = PORT_STREAM_UDP):
    """Runs a real-time terminal telemetry monitor using the view preset configuration."""
    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    sock.bind((listen_ip, port))

    active_tags = {s.tag: s for s in view.streams if s.enabled}
    print(f"\n" + "=" * 70)
    print(f" EmbeddedPixel Live Telemetry Dashboard : [{view.name}]")
    print(f" Active Channels: {', '.join(active_tags.keys())}")
    print(f" Listening on UDP port {port} (Press Ctrl+C to exit)...")
    print("=" * 70 + "\n")

    packet_counts: Dict[str, int] = {tag: 0 for tag in active_tags}
    last_values: Dict[str, Any] = {}
    last_time = time.time()
    
    csv_file = None
    csv_writer = None
    if view.export.get("save_csv", False):
        prefix = view.export.get("csv_prefix", "telemetry")
        ts_str = datetime.datetime.now().strftime("%Y%m%d_%H%M%S")
        csv_filename = f"{prefix}_{view.name}_{ts_str}.csv"
        csv_file = open(csv_filename, "w", newline="", encoding="utf-8")
        csv_writer = csv.writer(csv_file)
        csv_writer.writerow(["host_time_iso", "node_utc_us", "stream_tag", "value", "rate_hz", "seq_num"])
        print(f"[Export] Logging live data to CSV: {csv_filename}\n")

    try:
        while True:
            data, addr = sock.recvfrom(2048)
            if len(data) < 36:
                continue

            # Header
            magic, ver, flags, node_id, msg_type, seq_num, payload_len, _ = struct.unpack_from("<HBBHH I H H", data, 0)
            if magic != PE_MAGIC:
                continue

            # Stream header (offset 16)
            ts_us, tag_raw, rate_hz, sample_count, ch_count, sample_type = struct.unpack_from("<Q 4s HHHH", data, 16)
            tag = tag_raw.decode("ascii", errors="replace").strip("\x00")

            if tag not in active_tags:
                continue

            packet_counts[tag] += 1

            # Parse value
            val_num = 0
            if tag == "TEMP" and len(data) >= 40:
                val_num = struct.unpack_from("<i", data, 36)[0]
                val_str = f"{val_num:5.1f} °C"
            elif tag == "CNTR" and len(data) >= 40:
                val_num = struct.unpack_from("<I", data, 36)[0]
                val_str = f"{val_num:8d}"
            elif tag == "ADC0" and len(data) >= 38:
                val_num = struct.unpack_from("<H", data, 36)[0]
                val_str = f"{val_num:5d} mV"
            else:
                val_str = f"Raw len={len(data)-36}B"

            last_values[tag] = val_str

            if csv_writer:
                csv_writer.writerow([
                    datetime.datetime.now().isoformat(),
                    ts_us,
                    tag,
                    val_num,
                    rate_hz,
                    seq_num
                ])

            # Terminal render cadence (every ~100ms or on packet)
            now = time.time()
            if now - last_time >= (view.layout.get("refresh_rate_ms", 100) / 1000.0):
                last_time = now
                dt_utc = datetime.datetime.fromtimestamp(ts_us / 1e6, tz=datetime.timezone.utc) if ts_us > 1000000000000 else None
                ts_display = dt_utc.strftime("%H:%M:%S.%f")[:-3] if dt_utc else f"{ts_us/1e6:8.3f}s"
                
                status_parts = []
                for t, cfg in active_tags.items():
                    val = last_values.get(t, "---")
                    pkts = packet_counts.get(t, 0)
                    status_parts.append(f"[{t}] {val} ({cfg.rate_hz}Hz | {pkts}pkts)")
                
                sys.stdout.write(f"\r\033[K[Time: {ts_display}]  " + "  |  ".join(status_parts))
                sys.stdout.flush()

    except KeyboardInterrupt:
        print("\n\nDashboard stopped by user.")
    finally:
        sock.close()
        if csv_file:
            csv_file.close()
            print(f"[Export] Saved telemetry session log.")


# ─────────────────────────────────────────────────────────────────────────────
# CLI Commands
# ─────────────────────────────────────────────────────────────────────────────

def main():
    parser = argparse.ArgumentParser(description="EmbeddedPixel Telemetry View & Preset Manager")
    subparsers = parser.add_subparsers(dest="command", required=True)

    # list
    subparsers.add_parser("list", help="List all available telemetry view presets")

    # show
    show_p = subparsers.add_parser("show", help="Display details of a saved view preset")
    show_p.add_argument("name", help="Name or path of view preset")

    # apply
    apply_p = subparsers.add_parser("apply", help="Apply a saved view preset to a target node")
    apply_p.add_argument("name", help="Name or path of view preset")
    apply_p.add_argument("--ip", required=True, help="Target node IP address")

    # stream
    stream_p = subparsers.add_parser("stream", help="Apply view and start real-time telemetry dashboard")
    stream_p.add_argument("name", help="Name or path of view preset")
    stream_p.add_argument("--ip", default=None, help="Target node IP address (if omitted, listens without configuring)")
    stream_p.add_argument("--port", type=int, default=PORT_STREAM_UDP, help="UDP listening port (default: 50001)")

    # save
    save_p = subparsers.add_parser("save", help="Create and save a new view preset")
    save_p.add_argument("name", help="Name of new view preset")
    save_p.add_argument("--streams", nargs="+", required=True, help="Streams in TAG:RATE_HZ format (e.g. CNTR:50 TEMP:10)")
    save_p.add_argument("--desc", default="", help="Description of view")
    save_p.add_argument("--save-csv", action="store_true", help="Enable automatic CSV logging")

    # capture
    capture_p = subparsers.add_parser("capture", help="Capture live node stream configuration into a new view preset")
    capture_p.add_argument("name", help="Name of new view preset to create")
    capture_p.add_argument("--ip", required=True, help="Target node IP address")

    args = parser.parse_args()
    mgr = TelemetryViewManager()

    if args.command == "list":
        views = mgr.list_views()
        print("\n=== Available Telemetry View Presets ===")
        if not views:
            print("  (No saved views found)")
        for v in views:
            print(f"  * {v['name']:<20} : {v['description']}")
            print(f"      Streams: {', '.join(v['streams']) if v['streams'] else 'None'}  |  Path: {v['path']}")
        print("")

    elif args.command == "show":
        view = mgr.load_view(args.name)
        print(f"\n=== View Preset: [{view.name}] ===")
        print(f"Description: {view.description}")
        print(f"Target Node: #{view.node_id}")
        print(f"Layout Mode: {view.layout.get('mode', 'dashboard')}")
        print(f"CSV Logging: {view.export.get('save_csv', False)}")
        print("\nConfigured Streams:")
        for s in view.streams:
            status = "ENABLED" if s.enabled else "DISABLED"
            print(f"  * [{s.tag:<4}] {s.rate_hz:>4} Hz | {status:<8} | Title: '{s.display.title}' | Units: {s.display.units}")
        print("")


    elif args.command == "apply":
        view = mgr.load_view(args.name)
        mgr.apply_view(view, args.ip)

    elif args.command == "stream":
        view = mgr.load_view(args.name)
        if args.ip:
            mgr.apply_view(view, args.ip)
        run_stream_dashboard(view, port=args.port)

    elif args.command == "save":
        streams = []
        for item in args.streams:
            parts = item.split(":")
            tag = parts[0].upper()
            rate = int(parts[1]) if len(parts) > 1 else 10
            units = "°C" if tag == "TEMP" else ("counts" if tag == "CNTR" else "")
            streams.append(StreamConfig(tag=tag, rate_hz=rate, display=ChannelDisplay(title=f"{tag} Stream", units=units)))
        view = ViewConfig(
            name=args.name,
            description=args.desc,
            streams=streams,
            export={"save_csv": args.save_csv, "csv_prefix": args.name}
        )
        saved_path = mgr.save_view(view)
        print(f"[SUCCESS] Saved view preset '{view.name}' to: {saved_path}")

    elif args.command == "capture":
        view = mgr.capture_live_view(args.ip, args.name)
        saved_path = mgr.save_view(view)
        print(f"[SUCCESS] Captured active streams from {args.ip} into '{view.name}' -> {saved_path}")


if __name__ == "__main__":
    main()
