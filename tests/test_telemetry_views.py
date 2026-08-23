#!/usr/bin/env python3
"""
Unit Tests for EmbeddedPixel Telemetry View & Preset Manager
============================================================
Tests serialization, deserialization, search path resolution, and configuration validation.
"""

import json
import tempfile
import unittest
from pathlib import Path
import sys

# Add scripts directory to module path
SCRIPTS_DIR = Path(__file__).resolve().parent.parent / "scripts"
sys.path.insert(0, str(SCRIPTS_DIR))

from telemetry_views import (
    ChannelDisplay,
    StreamConfig,
    TelemetryViewManager,
    ViewConfig,
)


class TestTelemetryViews(unittest.TestCase):
    def setUp(self):
        self.temp_dir = tempfile.TemporaryDirectory()
        self.search_dir = Path(self.temp_dir.name)
        self.mgr = TelemetryViewManager(search_dirs=[self.search_dir])

    def tearDown(self):
        self.temp_dir.cleanup()

    def test_stream_config_serialization(self):
        disp = ChannelDisplay(title="Test Channel", units="counts", color="cyan", y_min=0, y_max=1000)
        stream = StreamConfig(tag="cntr", rate_hz=50, enabled=True, display=disp)

        d = stream.to_dict()
        self.assertEqual(d["tag"], "CNTR")
        self.assertEqual(d["rate_hz"], 50)
        self.assertTrue(d["enabled"])
        self.assertEqual(d["display"]["title"], "Test Channel")
        self.assertEqual(d["display"]["units"], "counts")

        # Roundtrip
        stream_restored = StreamConfig.from_dict(d)
        self.assertEqual(stream_restored.tag, "CNTR")
        self.assertEqual(stream_restored.rate_hz, 50)
        self.assertEqual(stream_restored.display.title, "Test Channel")

    def test_view_config_save_and_load(self):
        streams = [
            StreamConfig(tag="CNTR", rate_hz=20, display=ChannelDisplay(title="Counter")),
            StreamConfig(tag="TEMP", rate_hz=2, display=ChannelDisplay(title="Temperature", units="°C")),
        ]
        view = ViewConfig(
            name="test_view",
            description="A unit test view preset",
            node_id=1,
            streams=streams,
            layout={"mode": "split", "refresh_rate_ms": 50},
            export={"save_csv": True, "csv_prefix": "unit_test"}
        )

        saved_path = self.mgr.save_view(view)
        self.assertTrue(saved_path.exists())

        loaded = self.mgr.load_view("test_view")
        self.assertEqual(loaded.name, "test_view")
        self.assertEqual(loaded.description, "A unit test view preset")
        self.assertEqual(len(loaded.streams), 2)
        self.assertEqual(loaded.streams[0].tag, "CNTR")
        self.assertEqual(loaded.streams[0].rate_hz, 20)
        self.assertEqual(loaded.streams[1].tag, "TEMP")
        self.assertEqual(loaded.streams[1].rate_hz, 2)
        self.assertTrue(loaded.export["save_csv"])

    def test_list_views(self):
        # Create two views
        v1 = ViewConfig(name="view_alpha", description="First view", streams=[StreamConfig(tag="CNTR", rate_hz=10)])
        v2 = ViewConfig(name="view_beta", description="Second view", streams=[StreamConfig(tag="TEMP", rate_hz=5)])
        self.mgr.save_view(v1)
        self.mgr.save_view(v2)

        views = self.mgr.list_views()
        self.assertEqual(len(views), 2)
        names = [v["name"] for v in views]
        self.assertIn("view_alpha", names)
        self.assertIn("view_beta", names)

    def test_load_view_missing_raises_filenotfound(self):
        with self.assertRaises(FileNotFoundError):
            self.mgr.load_view("non_existent_view_12345")

    def test_default_built_in_views_exist_and_valid(self):
        built_in_mgr = TelemetryViewManager()
        views = built_in_mgr.list_views()
        self.assertGreaterEqual(len(views), 4)

        # Check default views can all be loaded without exception
        default_view = built_in_mgr.load_view("default")
        self.assertEqual(default_view.name, "default")
        self.assertTrue(any(s.tag == "CNTR" for s in default_view.streams))

        thermal_view = built_in_mgr.load_view("thermal_stress")
        self.assertEqual(thermal_view.name, "thermal_stress")
        self.assertTrue(any(s.tag == "TEMP" for s in thermal_view.streams))


if __name__ == "__main__":
    unittest.main()
