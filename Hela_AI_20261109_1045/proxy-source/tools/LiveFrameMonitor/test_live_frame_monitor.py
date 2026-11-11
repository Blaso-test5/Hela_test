#!/usr/bin/env python3
"""
Unit tests for tools/LiveFrameMonitor/live_frame_monitor.py.
Covers:
  - Delayed file creation, incremental partial-line append, and log truncation/rotation
  - Split/interleaved native Present #100 `[READBACK_SRC]` and `[READBACK]` headers + 5-point
    Top-left/Top-right/Center/Bottom-left/Bottom-right RGBA sample lines without `Non-black pixels:`
  - Black/alpha sample semantics (RGB=(0,0,0) is black regardless of alpha; RGB!=(0,0,0) is non-black)
  - Strict Present #100 slot isolation (never falls back to Present #200+ if #100 is absent)
  - `--stop-after-present100` immediate completion once both #100 readbacks are complete, with bounded
    BMP timeout when BMP files are absent
  - Manifest-pinned live module fingerprint attestation and configurable Present capture targets
  - Manager restoration of absent vanilla proxy files and legacy restore manifests
  - PowerShell `Start-HelaLiveMonitor.ps1` argument quoting for paths containing spaces
  - Atomic completeness detection for partial vs finished 24-bit BMP files
  - Bounded memory ring-buffer caps
"""

from __future__ import annotations

import hashlib
import json
import shutil
import struct
import subprocess
import tempfile
import time
import unittest
from pathlib import Path

import live_frame_monitor as lfm


TOOL_DIR = Path(__file__).resolve().parent
FIXTURE_LOG = TOOL_DIR / "fixtures" / "sample_live_stream.log"
LAUNCHER_PS1 = TOOL_DIR / "Start-HelaLiveMonitor.ps1"
PROXY_MANAGER_PS1 = TOOL_DIR / "ProxyManager.ps1"


def _make_24bit_bmp_bytes(width: int, height: int, fill_bgr: bytes = b"\x00\x00\x00") -> bytes:
    row_stride = ((width * 3 + 3) // 4) * 4
    padding = b"\x00" * (row_stride - width * 3)
    row_bytes = (fill_bgr * width) + padding
    pixel_data = row_bytes * height
    file_size = 54 + len(pixel_data)
    header = struct.pack(
        "<2sIHHIiiiHHIIIIII",
        b"BM",
        file_size,
        0,
        0,
        54,
        40,
        width,
        height,
        1,
        24,
        0,
        len(pixel_data),
        2835,
        2835,
        0,
        0,
    )
    return header + pixel_data


def _run_proxy_manager_restore(
    powershell: str,
    game_exe: Path,
    run_directory: Path,
) -> subprocess.CompletedProcess[str]:
    quote = lambda value: "'" + str(value).replace("'", "''") + "'"
    command = (
        f"& {quote(PROXY_MANAGER_PS1)} -Action Restore "
        f"-GameExePath {quote(game_exe)} -RunDirectory {quote(run_directory)} -Confirm:$false"
    )
    return subprocess.run(
        [
            powershell,
            "-NoProfile",
            "-ExecutionPolicy",
            "Bypass",
            "-Command",
            command,
        ],
        capture_output=True,
        text=True,
        check=False,
    )


class LiveFrameMonitorTests(unittest.TestCase):
    def test_file_creation_partial_append_and_truncation_reset(self) -> None:
        events = []
        state = lfm.LiveMonitorState(event_callback=lambda s, e: events.append((s, e)))

        with tempfile.TemporaryDirectory() as tmpdir:
            log_path = Path(tmpdir) / "d3d12on11.log"
            tailer = lfm.LogFileTailer(log_path, state=state)

            # 1. File does not exist yet
            self.assertEqual(tailer.poll(), 0)

            # 2. Write a partial line without newline: must NOT be parsed yet
            log_path.write_bytes(b"[SHADER PLACEHOLDER] stage=CS hash=d17286aa32897a75 ")
            self.assertEqual(tailer.poll(), 0)
            self.assertEqual(len(state.shaders_placeholder), 0)

            # 3. Complete the line and add another
            with log_path.open("ab") as f:
                f.write(b"reason=no-usable-SM5-bytecode\n")
                f.write(b"Resource 0000024096331C38 operations=66 draws=66 skipped=0 clears=0\n")
            self.assertEqual(tailer.poll(), 2)
            self.assertIn("d17286aa32897a75", state.shaders_placeholder)
            self.assertIn("0000024096331C38", state.resources_by_ptr)
            self.assertEqual(state.generation, 1)

            # 4. Truncate/rotate the log file to a shorter file
            log_path.write_bytes(b"[SHADER READY] stage=VS hash=20e0826b236597a1\n")
            fed = tailer.poll()
            self.assertEqual(fed, 1)
            self.assertEqual(state.generation, 2)
            self.assertEqual(state.truncation_count, 1)
            self.assertEqual(len(state.shaders_placeholder), 0)
            self.assertIn("20e0826b236597a1", state.shaders_ready)

    def test_interleaved_v001_native_readback_headers_and_5point_samples(self) -> None:
        """
        Regression test for the GTX 950M v001 native log shape:
        - `[READBACK_SRC] Present #100 CopyResource source=...` is followed by interleaved shader/thread lines
        - Then a second header `[READBACK_SRC] Present #100 size=1280x720 format=24` appears
        - Followed by Top-left / Top-right / Center / Bottom-left / Bottom-right RGBA sample lines
          (with NO `Non-black pixels:` line)
        - Followed by analogous split/interleaved `[READBACK] Present #100` lines.
        """
        events = []
        state = lfm.LiveMonitorState(event_callback=lambda s, e: events.append((s, e)))

        v001_lines = [
            "Resource 0000024096331C38 operations=66 draws=66 skipped=0 clears=0",
            "  DrawIndexedInstanced VS=20e0826b236597a1 PS=44b0a992c2304254 elements=6 instances=1 RTV slot=0 viewport=1280x720",
            "  DrawIndexedInstanced VS=20e0826b236597a1 PS=d77600bde0076c33 elements=6 instances=1 RTV slot=0 viewport=1280x720",
            "[READBACK_SRC] Present #100 CopyResource source=0000024096331C38 dimension=3 width=1280 height=720 format=24 sequence=100",
            "[SHADER PLACEHOLDER] stage=CS hash=d17286aa32897a75 reason=no-usable-SM5-bytecode",
            "[OTHER_THREAD] asynchronous pipeline cache diagnostic line",
            "[READBACK_SRC] Present #100 size=1280x720 format=24",
            "  Top-left (0, 0): RGBA=(0, 0, 0, 0)",
            "  Top-right (1279, 0): RGBA=(0, 0, 0, 0)",
            "[SHADER READY] stage=VS hash=20e0826b236597a1 source=override model=sm_5_0",
            "  Center (640, 360): RGBA=(0, 0, 0, 0)",
            "  Bottom-left (0, 719): RGBA=(0, 0, 0, 0)",
            "  Bottom-right (1279, 719): RGBA=(0, 0, 0, 0)",
            "[READBACK] Present #100 size=1280x720 format=24 buffer=0",
            "[SHADER PLACEHOLDER] stage=CS hash=fb1aa7e82eb127e4 reason=no-usable-SM5-bytecode",
            "  Top-left (0, 0): RGBA=(0, 0, 0, 0)",
            "  Top-right (1279, 0): RGBA=(0, 0, 0, 0)",
            "  Center (640, 360): RGBA=(0, 0, 0, 0)",
            "  Bottom-left (0, 719): RGBA=(0, 0, 0, 0)",
            "  Bottom-right (1279, 719): RGBA=(0, 0, 0, 0)",
        ]

        for line in v001_lines:
            state.feed_line(line)

        self.assertTrue(state.present100_reached())
        report = state.build_report("v001_native.log")
        p100 = report["present_100"]

        rb_src = p100["source_readback"]
        rb_bb = p100["backbuffer_readback"]
        self.assertIsNotNone(rb_src)
        self.assertIsNotNone(rb_bb)

        # Merged metadata from both [READBACK_SRC] Present #100 header lines
        self.assertEqual(rb_src["present"], 100)
        self.assertEqual(rb_src["source"], "0000024096331C38")
        self.assertEqual(rb_src["dimension"], 3)
        self.assertEqual(rb_src["width"], 1280)
        self.assertEqual(rb_src["height"], 720)
        self.assertEqual(rb_src["format"], 24)
        self.assertEqual(rb_src["sequence"], 100)
        self.assertEqual(len(rb_src["sample_pixels_rgba"]), 5)
        self.assertEqual(rb_src["non_black_count"], 0)
        self.assertEqual(rb_src["total_sampled"], 5)
        self.assertEqual(rb_src["non_black_pct"], 0.0)
        self.assertTrue(rb_src["emitted_complete_event"])

        self.assertEqual(rb_bb["present"], 100)
        self.assertEqual(rb_bb["width"], 1280)
        self.assertEqual(rb_bb["height"], 720)
        self.assertEqual(rb_bb["format"], 24)
        self.assertEqual(rb_bb["buffer"], 0)
        self.assertEqual(len(rb_bb["sample_pixels_rgba"]), 5)
        self.assertEqual(rb_bb["non_black_count"], 0)
        self.assertEqual(rb_bb["total_sampled"], 5)
        self.assertEqual(rb_bb["non_black_pct"], 0.0)
        self.assertTrue(rb_bb["emitted_complete_event"])

        # Ensure live events were emitted for both READBACK_SRC and READBACK_BB at Present #100
        event_types = [evt["event_type"] for _s, evt in events]
        self.assertIn("READBACK_SRC", event_types)
        self.assertIn("READBACK_BB", event_types)

    def test_v003_native_bb_source_history_draw_line_parsing(self) -> None:
        state = lfm.LiveMonitorState()
        state.feed_line(
            "[BB_SOURCE_HISTORY] Resource 000002844D502778 "
            "operations=64 draws=64 skipped=0 clears=0"
        )
        state.feed_line(
            "[BB_SOURCE_HISTORY]   Draw #7 api=DrawIndexedInstanced indexed=1 rtv_slot=0 "
            "pso=0000028463A15200 vs_hash=20e0826b236597a1 ps_hash=44b0a992c2304254 "
            "elements=6 instances=1 viewport=(0.0,0.0,1280.0,720.0)"
        )

        history = state.resources_by_ptr["000002844D502778"]
        self.assertEqual(len(history.retained_draws), 1)
        draw = history.retained_draws[0]
        self.assertEqual(draw.draw_type, "DrawIndexedInstanced")
        self.assertEqual(draw.vs, "20e0826b236597a1")
        self.assertEqual(draw.ps, "44b0a992c2304254")
        self.assertEqual(draw.elements, 6)
        self.assertEqual(draw.instances, 1)
        self.assertEqual(draw.rtv_slot, 0)
        self.assertEqual(draw.viewport, "1280x720")

    def test_black_and_alpha_rgba_sample_semantics(self) -> None:
        """
        Verifies black/alpha semantics:
        - RGBA=(0, 0, 0, 255) has RGB=(0,0,0) -> counted as black (non-black=False), though all_samples_zero=False
        - RGBA=(12, 0, 0, 0) has R=12 -> counted as non-black
        """
        state = lfm.LiveMonitorState()
        lines = [
            "[READBACK_SRC] Present #100 CopyResource source=0000024096331C38 width=1280 height=720 format=24 sequence=100",
            "  Top-left: RGBA=(0, 0, 0, 255)",
            "  Top-right: RGBA=(0, 0, 0, 0)",
            "  Center: RGBA=(16, 32, 64, 255)",
            "  Bottom-left: RGBA=(0, 0, 0, 0)",
            "  Bottom-right: RGBA=(0, 5, 0, 0)",
        ]
        for line in lines:
            state.feed_line(line)

        rb = state.present100_readback_src
        self.assertIsNotNone(rb)
        assert rb is not None
        self.assertTrue(rb.emitted_complete_event)
        self.assertEqual(rb.non_black_count, 2)
        self.assertEqual(rb.total_sampled, 5)
        self.assertEqual(rb.non_black_pct, 40.0)
        self.assertFalse(rb.all_samples_zero)
        self.assertFalse(rb.all_samples_rgb_black)

    def test_no_false_present100_fallback_when_only_later_presents_exist(self) -> None:
        """
        Ensures that if a log only contains Present #200 readbacks, `present_100` in the report
        never misreports Present #200 as if it were Present #100, and `present100_reached()` is False.
        """
        state = lfm.LiveMonitorState()
        lines = [
            "[READBACK_SRC] Present #200 CopyResource source=0000024096331C38 width=1280 height=720 format=24 sequence=200",
            "  Top-left: RGBA=(0, 0, 0, 0)",
            "  Top-right: RGBA=(0, 0, 0, 0)",
            "  Center: RGBA=(0, 0, 0, 0)",
            "  Bottom-left: RGBA=(0, 0, 0, 0)",
            "  Bottom-right: RGBA=(0, 0, 0, 0)",
            "[READBACK] Present #200 size=1280x720 format=24 buffer=0",
            "  Top-left: RGBA=(0, 0, 0, 0)",
            "  Top-right: RGBA=(0, 0, 0, 0)",
            "  Center: RGBA=(0, 0, 0, 0)",
            "  Bottom-left: RGBA=(0, 0, 0, 0)",
            "  Bottom-right: RGBA=(0, 0, 0, 0)",
        ]
        for line in lines:
            state.feed_line(line)

        self.assertFalse(state.present100_reached())
        report = state.build_report("only_present200.log")
        self.assertIsNone(report["present_100"]["source_readback"])
        self.assertIsNone(report["present_100"]["backbuffer_readback"])

    def test_run_manifest_module_fingerprint_attestation(self) -> None:
        with tempfile.TemporaryDirectory() as tmpdir:
            manifest_path = Path(tmpdir) / "run-manifest.json"
            expected_d3d12 = "A" * 64
            expected_dxgi = "B" * 64
            manifest_path.write_text(
                json.dumps(
                    {
                        "schema": "hela-run-manifest/v1",
                        "runtimeAttestation": "module-and-shader-sha256",
                        "testVersion": "unit-test-v001",
                        "runtime": {"d3d12": expected_d3d12, "dxgi": expected_dxgi},
                        "overrides": {},
                        "requiredOverrides": [],
                    }
                ),
                encoding="utf-8",
            )

            manifest = lfm.load_run_manifest(manifest_path)
            state = lfm.LiveMonitorState(run_manifest=manifest)
            self.assertEqual(state.run_identity_report()["attestation_status"], "pending")

            state.feed_line(f"MODULE_FINGERPRINT role=d3d12 sha256={expected_d3d12.lower()}")
            state.feed_line(f"MODULE_FINGERPRINT role=dxgi_proxy sha256={expected_dxgi.lower()}")
            self.assertEqual(state.run_identity_report()["attestation_status"], "verified")

            state.feed_line(f"MODULE_FINGERPRINT role=d3d12 sha256={'c' * 64}")
            self.assertEqual(state.run_identity_report()["attestation_status"], "mismatch")

            state.feed_line("MODULE_FINGERPRINT_FAILED role=dxgi_proxy reason=read-failed")
            identity = state.run_identity_report()
            self.assertEqual(identity["attestation_status"], "failed")
            self.assertEqual(identity["module_fingerprint_failures"]["dxgi"], "read-failed")

            empty_manifest_state = lfm.LiveMonitorState(
                run_manifest={
                    "test_version": "invalid-empty-pins",
                    "manifest_path": str(manifest_path),
                    "expected_modules": {},
                }
            )
            self.assertEqual(
                empty_manifest_state.run_identity_report()["attestation_status"],
                "pending",
            )

    def test_configurable_present_target_isolated_from_present100(self) -> None:
        with tempfile.TemporaryDirectory() as tmpdir:
            log_path = Path(tmpdir) / "present400.log"
            log_path.write_text(
                "\n".join(
                    [
                        "[READBACK_SRC] Present #400 CopyResource source=0000024096331C38 width=1280 height=720 format=24 sequence=400",
                        "  Top-left: RGBA=(1, 0, 0, 255)",
                        "  Top-right: RGBA=(1, 0, 0, 255)",
                        "  Center: RGBA=(1, 0, 0, 255)",
                        "  Bottom-left: RGBA=(1, 0, 0, 255)",
                        "  Bottom-right: RGBA=(1, 0, 0, 255)",
                        "[READBACK] Present #400 size=1280x720 format=24 buffer=0",
                        "  Top-left: RGBA=(1, 0, 0, 255)",
                        "  Top-right: RGBA=(1, 0, 0, 255)",
                        "  Center: RGBA=(1, 0, 0, 255)",
                        "  Bottom-left: RGBA=(1, 0, 0, 255)",
                        "  Bottom-right: RGBA=(1, 0, 0, 255)",
                    ]
                )
                + "\n",
                encoding="utf-8",
            )

            rc, report = lfm.run_monitor(
                log_path=log_path,
                poll_interval_sec=0.01,
                stop_after_present_num=400,
                max_wait_sec=2.0,
                quiet_events=True,
            )

        self.assertEqual(rc, 0)
        self.assertTrue(report["capture_target"]["complete"])
        self.assertEqual(report["capture_target"]["present"], 400)
        self.assertEqual(report["capture_target"]["source_readback"]["present"], 400)
        self.assertEqual(report["capture_target"]["source_readback"]["non_black_count"], 5)
        self.assertIsNone(report["present_100"]["source_readback"])

    def test_stop_after_present100_terminates_promptly_with_bounded_missing_bmp_timeout(self) -> None:
        with tempfile.TemporaryDirectory() as tmpdir:
            tmp_path = Path(tmpdir)
            log_path = tmp_path / "live_v001.log"
            missing_bmp = tmp_path / "never_written_source.bmp"

            log_path.write_text(
                "\n".join(
                    [
                        "[READBACK_SRC] Present #100 CopyResource source=0000024096331C38 width=1280 height=720 format=24 sequence=100",
                        "  Top-left: RGBA=(0, 0, 0, 0)",
                        "  Top-right: RGBA=(0, 0, 0, 0)",
                        "  Center: RGBA=(0, 0, 0, 0)",
                        "  Bottom-left: RGBA=(0, 0, 0, 0)",
                        "  Bottom-right: RGBA=(0, 0, 0, 0)",
                        "[READBACK] Present #100 size=1280x720 format=24 buffer=0",
                        "  Top-left: RGBA=(0, 0, 0, 0)",
                        "  Top-right: RGBA=(0, 0, 0, 0)",
                        "  Center: RGBA=(0, 0, 0, 0)",
                        "  Bottom-left: RGBA=(0, 0, 0, 0)",
                        "  Bottom-right: RGBA=(0, 0, 0, 0)",
                    ]
                )
                + "\n",
                encoding="utf-8",
            )

            t0 = time.monotonic()
            rc, report = lfm.run_monitor(
                log_path=log_path,
                poll_interval_sec=0.02,
                once=False,
                stop_after_present100=True,
                max_wait_sec=5.0,
                source_bmp_path=missing_bmp,
                bmp_wait_timeout_sec=0.10,
                quiet_events=True,
            )
            elapsed = time.monotonic() - t0
            self.assertEqual(rc, 0)
            self.assertLess(elapsed, 1.5)
            self.assertEqual(report["present_100"]["source_readback"]["present"], 100)
            self.assertEqual(report["present_100"]["backbuffer_readback"]["present"], 100)

    def test_powershell_launcher_argument_quoting_with_spaces(self) -> None:
        pwsh = shutil.which("powershell") or shutil.which("pwsh")
        if not pwsh or not LAUNCHER_PS1.is_file():
            self.skipTest("PowerShell not available in test environment")

        spaced_log = r"D:\SteamLibrary\steamapps\common\Hela Demo\d3d12on11.log"
        spaced_json = r"D:\Ghidra MCP\Hela-Export\tools\LiveFrameMonitor\out\report with spaces.json"
        spaced_bmp = r"D:\Ghidra MCP\Hela-Export\tools\LiveFrameMonitor\out\src 100.bmp"
        spaced_manifest = r"D:\Ghidra MCP\Hela-Export\evidence\run manifest.json"

        proc = subprocess.run(
            [
                pwsh,
                "-NoProfile",
                "-ExecutionPolicy",
                "Bypass",
                "-File",
                str(LAUNCHER_PS1),
                "-LogPath",
                spaced_log,
                "-JsonOutPath",
                spaced_json,
                "-SourceBmpPath",
                spaced_bmp,
                "-RunManifestPath",
                spaced_manifest,
                "-StopAfterPresent",
                "400",
                "-MonitorMaxWaitSeconds",
                "180",
                "-PrintCommandLineOnly",
            ],
            capture_output=True,
            text=True,
            check=True,
        )
        cmd_line = proc.stdout.strip()
        self.assertIn(f'"{spaced_log}"', cmd_line)
        self.assertIn(f'"{spaced_json}"', cmd_line)
        self.assertIn(f'"{spaced_bmp}"', cmd_line)
        self.assertIn(f'"{spaced_manifest}"', cmd_line)
        self.assertIn("--stop-after-present 400", cmd_line)
        self.assertIn("--max-wait 180", cmd_line)
        self.assertIn("--run-manifest", cmd_line)

    def test_proxy_manager_restores_missing_and_legacy_runtime_files(self) -> None:
        powershell = shutil.which("powershell") or shutil.which("pwsh")
        if not powershell or not PROXY_MANAGER_PS1.is_file():
            self.skipTest("PowerShell or ProxyManager.ps1 not available in test environment")

        with tempfile.TemporaryDirectory() as tmpdir:
            root = Path(tmpdir)
            game_dir = root / "game"
            patch_dir = game_dir / "D3D12On11Patch"
            installed_overrides = patch_dir / "overrides"
            restore_run = root / "run-no-proxy"
            restore_root = restore_run / "restore-point"
            game_dir.mkdir(parents=True)
            restore_root.mkdir(parents=True)
            installed_overrides.mkdir(parents=True)
            game_exe = game_dir / "Hela-Win64-Shipping.exe"
            game_exe.write_bytes(b"synthetic executable placeholder")
            for name in ("d3d12.dll", "dxgi.dll"):
                (game_dir / name).write_bytes(b"temporary installed proxy")
            (installed_overrides / "temporary.cso").write_bytes(b"temporary override")

            absent_runtime_manifest = {
                "schema": "hela-proxy-restore/v1",
                "game_directory": str(game_dir),
                "created_utc": "2026-11-11T00:00:00Z",
                "runtime": {
                    "d3d12.dll": {"present": False, "sha256": None},
                    "dxgi.dll": {"present": False, "sha256": None},
                },
                "overrides_directory_present": False,
                "overrides": [],
            }
            (restore_root / "restore-manifest.json").write_text(
                json.dumps(absent_runtime_manifest),
                encoding="utf-8",
            )

            absent_restore = _run_proxy_manager_restore(powershell, game_exe, restore_run)
            self.assertEqual(
                absent_restore.returncode,
                0,
                absent_restore.stdout + "\n" + absent_restore.stderr,
            )
            self.assertFalse((game_dir / "d3d12.dll").exists())
            self.assertFalse((game_dir / "dxgi.dll").exists())
            self.assertFalse(installed_overrides.exists())

            legacy_run = root / "run-legacy"
            legacy_restore = legacy_run / "restore-point"
            legacy_overrides = legacy_restore / "overrides"
            legacy_overrides.mkdir(parents=True)
            legacy_runtime = {}
            for name in ("d3d12.dll", "dxgi.dll"):
                content = f"original {name}".encode("ascii")
                (legacy_restore / name).write_bytes(content)
                legacy_runtime[name] = hashlib.sha256(content).hexdigest()
            original_shader = b"original shader"
            (legacy_overrides / "original.cso").write_bytes(original_shader)
            (legacy_restore / "restore-manifest.json").write_text(
                json.dumps(
                    {
                        "schema": "hela-proxy-restore/v1",
                        "game_directory": str(game_dir),
                        "created_utc": "2026-11-11T00:00:00Z",
                        "runtime": legacy_runtime,
                        "overrides": [
                            {
                                "path": "original.cso",
                                "bytes": len(original_shader),
                                "sha256": hashlib.sha256(original_shader).hexdigest(),
                            }
                        ],
                    }
                ),
                encoding="utf-8",
            )

            legacy_restore_result = _run_proxy_manager_restore(powershell, game_exe, legacy_run)
            self.assertEqual(
                legacy_restore_result.returncode,
                0,
                legacy_restore_result.stdout + "\n" + legacy_restore_result.stderr,
            )
            self.assertEqual((game_dir / "d3d12.dll").read_bytes(), b"original d3d12.dll")
            self.assertEqual((game_dir / "dxgi.dll").read_bytes(), b"original dxgi.dll")
            self.assertEqual((installed_overrides / "original.cso").read_bytes(), original_shader)
            self.assertFalse((installed_overrides / "temporary.cso").exists())

    def test_bmp_incomplete_vs_complete_inspection(self) -> None:
        with tempfile.TemporaryDirectory() as tmpdir:
            bmp_path = Path(tmpdir) / "source_100.bmp"
            full_bytes = _make_24bit_bmp_bytes(4, 4, fill_bgr=b"\x00\x00\x00")

            # Write only the first 60 bytes (incomplete pixel payload)
            bmp_path.write_bytes(full_bytes[:60])
            self.assertIsNone(lfm.try_inspect_complete_bmp(bmp_path, label="source"))

            # Now write the full BMP
            bmp_path.write_bytes(full_bytes)
            info = lfm.try_inspect_complete_bmp(bmp_path, label="source")
            self.assertIsNotNone(info)
            assert info is not None
            self.assertEqual(info.width, 4)
            self.assertEqual(info.height, 4)
            self.assertEqual(info.total_pixels, 16)
            self.assertEqual(info.non_black_pixels, 0)

            # Non-black BMP check (red pixels: BGR = 0, 0, 255)
            red_bmp_path = Path(tmpdir) / "bb_red.bmp"
            red_bmp_path.write_bytes(_make_24bit_bmp_bytes(2, 2, fill_bgr=b"\x00\x00\xff"))
            red_info = lfm.try_inspect_complete_bmp(red_bmp_path, label="backbuffer")
            self.assertIsNotNone(red_info)
            assert red_info is not None
            self.assertEqual(red_info.non_black_pixels, 4)
            self.assertEqual(red_info.first_non_black_rgb, (255, 0, 0))

    def test_fixture_once_mode_and_epistemic_separation(self) -> None:
        with tempfile.TemporaryDirectory() as tmpdir:
            tmp_path = Path(tmpdir)
            json_out = tmp_path / "live_report.json"
            src_bmp = tmp_path / "src.bmp"
            bb_bmp = tmp_path / "bb.bmp"
            src_bmp.write_bytes(_make_24bit_bmp_bytes(4, 2, fill_bgr=b"\x00\x00\x00"))
            bb_bmp.write_bytes(_make_24bit_bmp_bytes(4, 2, fill_bgr=b"\x00\x00\x00"))

            rc = lfm.main(
                [
                    str(FIXTURE_LOG),
                    "--once",
                    "--source-bmp",
                    str(src_bmp),
                    "--backbuffer-bmp",
                    str(bb_bmp),
                    "--json",
                    str(json_out),
                    "--quiet-events",
                ]
            )
            self.assertEqual(rc, 0)
            self.assertTrue(json_out.is_file())

            report = json.loads(json_out.read_text(encoding="utf-8"))
            self.assertEqual(report["schema"], "hela.live_frame_monitor")
            p100 = report["present_100"]
            self.assertEqual(p100["source_readback"]["source"], "0000024096331C38")
            self.assertEqual(p100["matched_source_history"]["resource"], "0000024096331C38")
            self.assertEqual(p100["matched_source_history"]["draws"], 66)
            self.assertEqual(report["bmp_verification"]["source"]["non_black_pixels"], 0)
            self.assertEqual(report["bmp_verification"]["backbuffer"]["non_black_pixels"], 0)

            # Verify epistemic guardrails
            hyps = "\n".join(report["hypotheses_and_open_questions"])
            self.assertIn("does NOT validate CopyResource", hyps)
            self.assertIn("does NOT by itself prove whether their Dispatch calls were skipped", hyps)

    def test_bounded_memory_caps(self) -> None:
        state = lfm.LiveMonitorState()
        state.feed_line("Resource 0000024096331C38 operations=100 draws=100 skipped=0 clears=0")
        for i in range(50):
            state.feed_line(
                f"  DrawIndexedInstanced VS=20e0826b236597a1 PS=44b0a992c2304254 elements=6 instances=1 RTV slot=0 viewport=1280x720"
            )
        hist = state.resources_by_ptr["0000024096331C38"]
        self.assertEqual(len(hist.retained_draws), lfm.MAX_RETAINED_DRAWS_PER_RESOURCE)


if __name__ == "__main__":
    unittest.main()
