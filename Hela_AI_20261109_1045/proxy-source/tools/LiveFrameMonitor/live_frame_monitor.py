#!/usr/bin/env python3
"""
live_frame_monitor.py - Real-time streaming log & BMP monitor for Hela D3D12-to-D3D11 proxy runs.

Designed to run alongside Hela-Win64-Shipping.exe and tail `d3d12on11.log` (where each line is
flushed via `fflush(g_logFile)`), handling:
  - Missing file at startup (waits for creation)
  - Incremental line appends (buffered until newline so partial lines are never misparsed)
  - Log truncation / rotation (resets file offset and increments generation counter)
  - Multi-threaded / interleaved readback blocks correlated by (kind, present) across split headers
  - Both 5-point native sample lines (Top-left/Top-right/Center/Bottom-left/Bottom-right) and
    optional explicit `Non-black pixels: X/Y (Z%)` summary lines
  - Strict Present #100 slot isolation (never falls back to Present #200+ in `present_100` report)
  - Bounded memory (ring buffers / deques with fixed capacities for all transient state)
  - Optional live detection and atomic completeness check of source/backbuffer 24/32-bit BMP dumps
  - Strict separation between verified log facts and open diagnostic hypotheses
"""

from __future__ import annotations

import argparse
import dataclasses
import json
import os
import re
import signal
import struct
import sys
import time
from collections import OrderedDict, deque
from pathlib import Path
from typing import Any, Callable, Deque, Dict, List, Optional, Tuple

SCHEMA_ID = "hela.live_frame_monitor"
SCHEMA_VERSION = "1.2.0"

# Bounded memory limits
MAX_RETAINED_DRAWS_PER_RESOURCE = 20
MAX_TRACKED_RESOURCES = 16
MAX_TRACKED_READBACK_PRESENTS = 16
MAX_TRACKED_SHADERS = 256
MAX_RECENT_COPY_TO_BB = 16
MAX_HEARTBEATS = 10
MAX_DISPATCH_CLUES = 25
MAX_LIVE_EVENTS = 100

# Default number of named sample points logged by native proxy readback
EXPECTED_POINT_SAMPLES = 5

RE_HEX_PTR = r"(?:0x)?([0-9A-Fa-f]{8,16})"
RE_HEX_HASH = r"(?:0x)?([0-9A-Fa-f]{16})"

RE_HISTORY_RESOURCE = re.compile(
    r"\bResource[:=\s]+" + RE_HEX_PTR + r".*?"
    r"\boperations=(\d+)\s+draws=(\d+)\s+skipped=(\d+)\s+clears=(\d+)",
    re.IGNORECASE,
)
RE_API_DRAW_ENTRY = re.compile(
    r"\bapi=(DrawIndexedInstanced|DrawInstanced|Draw)\b", re.IGNORECASE
)
RE_DRAW_ENTRY = re.compile(r"\b(DrawIndexedInstanced|DrawInstanced|Draw)\b", re.IGNORECASE)
RE_VS_HASH = re.compile(r"\bVS(?:_hash)?[:=\s]+" + RE_HEX_HASH, re.IGNORECASE)
RE_PS_HASH = re.compile(r"\bPS(?:_hash)?[:=\s]+" + RE_HEX_HASH, re.IGNORECASE)
RE_ELEMENTS = re.compile(r"\b(?:elements|indices|indexCount|verts)[:=\s]+(\d+)", re.IGNORECASE)
RE_INSTANCES = re.compile(r"\b(?:instances|instanceCount)[:=\s]+(\d+)", re.IGNORECASE)
RE_RTV_SLOT = re.compile(r"\b(?:rtv(?:[\s_]*slot)?|slot)[:=\s]+(\d+)", re.IGNORECASE)
RE_VIEWPORT = re.compile(
    r"\bviewport[:=\s]+(?:"
    r"(?P<width>\d+(?:\.\d+)?)\s*[xX]\s*(?P<height>\d+(?:\.\d+)?)"
    r"|\(\s*-?\d+(?:\.\d+)?\s*,\s*-?\d+(?:\.\d+)?\s*,\s*"
    r"(?P<tuple_width>\d+(?:\.\d+)?)\s*,\s*(?P<tuple_height>\d+(?:\.\d+)?)\s*\))",
    re.IGNORECASE,
)

# Matches ANY `[READBACK_SRC] Present #N ...` line (both `CopyResource source=...` and `size=WxH...`)
RE_READBACK_SRC_HEADER = re.compile(
    r"\[READBACK_SRC\]\s*Present\s*#(\d+)\b(.*)",
    re.IGNORECASE,
)
# Matches ANY `[READBACK] Present #N ...` line
RE_READBACK_BB_HEADER = re.compile(
    r"\[READBACK\]\s*Present\s*#(\d+)\b(.*)",
    re.IGNORECASE,
)
# Optional tag prefix on continuation/sample lines: `[READBACK_SRC]` or `[READBACK]`
RE_READBACK_TAG_PREFIX = re.compile(r"^\s*\[(READBACK_SRC|READBACK)\]\s*(.*)", re.IGNORECASE)
RE_SOURCE_PTR = re.compile(r"\bsource=" + RE_HEX_PTR, re.IGNORECASE)

RE_NON_BLACK = re.compile(
    r"Non-black\s+pixels:\s*(\d+)\s*/\s*(\d+)\s*\(\s*([0-9.]+)%\s*\)",
    re.IGNORECASE,
)

# Named 5-point sample indicator (Top-left, Top-right, Center, Bottom-left, Bottom-right, or Sample)
RE_SAMPLE_LABEL = re.compile(
    r"\b(Top-left|Top-right|Center|Bottom-left|Bottom-right|Sample)\b",
    re.IGNORECASE,
)

# Flexible RGBA extractor supporting:
#   - RGBA=(0, 0, 0, 0) or RGBA: (0,0,0,0) or RGBA=(0,0,0,0)
#   - R=0 G=0 B=0 A=0 or R:0 G:0 B:0 A:0
#   - RGBA=[0, 0, 0, 0] or RGBA 0 0 0 0
#   - Named point line with `(...): (r, g, b, a)`
RE_RGBA_TUPLE = re.compile(
    r"(?:RGBA\s*[:=]?\s*[\(\[]?\s*(\d+)\s*[,\s]\s*(\d+)\s*[,\s]\s*(\d+)\s*[,\s]\s*(\d+)\s*[\)\]]?"
    r"|\bR\s*[:=]\s*(\d+)\s*[,\s]\s*G\s*[:=]\s*(\d+)\s*[,\s]\s*B\s*[:=]\s*(\d+)\s*[,\s]\s*A\s*[:=]\s*(\d+)"
    r"|(?:Top-left|Top-right|Center|Bottom-left|Bottom-right)\b[^(\n]*?(?:\(\s*\d+\s*,\s*\d+\s*\)\s*[:=]?\s*)?"
    r"\(\s*(\d+)\s*,\s*(\d+)\s*,\s*(\d+)\s*,\s*(\d+)\s*\))",
    re.IGNORECASE,
)

RE_COPY_TO_BB = re.compile(r"\[?COPY_TO_BB\]?.*?\bsource=" + RE_HEX_PTR + r"(.*)", re.IGNORECASE)
RE_SEQ = re.compile(r"\bsequence=(\d+)", re.IGNORECASE)
RE_KEY_INT = re.compile(r"\b(dimension|width|height|format|sequence|buffer)=(\d+)", re.IGNORECASE)
RE_SIZE_WH = re.compile(r"\bsize=(\d+)x(\d+)", re.IGNORECASE)

RE_SHADER_READY = re.compile(r"\bSHADER\s+READY\b", re.IGNORECASE)
RE_SHADER_PLACEHOLDER = re.compile(r"\bSHADER\s+PLACEHOLDER\b", re.IGNORECASE)
RE_MODULE_FINGERPRINT = re.compile(
    r"\bMODULE_FINGERPRINT\s+role=(d3d12|dxgi_proxy)\b.*?"
    r"\bsha256=([0-9a-fA-F]{64})\b",
    re.IGNORECASE,
)
RE_MODULE_FINGERPRINT_FAILED = re.compile(
    r"\bMODULE_FINGERPRINT_FAILED\s+role=(d3d12|dxgi_proxy)\b.*?"
    r"\breason=([^\s,;]+)",
    re.IGNORECASE,
)
RE_SHADER_STAGE = re.compile(
    r"\b(?:stage|type)[:=\s]*(VS|PS|CS|GS|HS|DS|compute|vertex|pixel)\b|\b(VS|PS|CS)\b",
    re.IGNORECASE,
)
RE_REASON = re.compile(r"\breason=([^\s,;]+)", re.IGNORECASE)
RE_ANY_HASH16 = re.compile(r"\b([0-9a-fA-F]{16})\b")

RE_HEARTBEAT = re.compile(
    r"\bpresent=(\d+)\b.*?\bdraw(?:\(ok\))?=(\d+)\b.*?\bskip-vs=(\d+)\b",
    re.IGNORECASE,
)
RE_HB_KV = re.compile(r"([a-zA-Z0-9_-]+(?:\([a-zA-Z0-9_-]+\))?)=(\d+)")
RE_DISPATCH_CLUE = re.compile(
    r"\b(?:skipDispatches|skip-dispatch|skip-cs|dispatch\s+skipped|Dispatch\()",
    re.IGNORECASE,
)


def norm_ptr(ptr: str) -> str:
    return ptr.upper().zfill(16)


def norm_hash(h: str) -> str:
    return h.lower()


def load_run_manifest(manifest_path: Path) -> Dict[str, Any]:
    try:
        manifest = json.loads(manifest_path.read_text(encoding="utf-8-sig"))
    except (OSError, UnicodeDecodeError, json.JSONDecodeError) as exc:
        raise ValueError(f"Unable to read run manifest '{manifest_path}': {exc}") from exc

    if not isinstance(manifest, dict):
        raise ValueError(f"Invalid run manifest '{manifest_path}': expected a JSON object")
    if (
        manifest.get("schema") != "hela-run-manifest/v1"
        or manifest.get("runtimeAttestation") != "module-and-shader-sha256"
        or not isinstance(manifest.get("testVersion"), str)
        or not manifest["testVersion"].strip()
    ):
        raise ValueError(f"Invalid run manifest identity: {manifest_path}")

    runtime = manifest.get("runtime")
    if not isinstance(runtime, dict):
        raise ValueError(f"Invalid run manifest runtime section: {manifest_path}")
    expected_modules: Dict[str, str] = {}
    for role in ("d3d12", "dxgi"):
        digest = runtime.get(role)
        if not isinstance(digest, str) or not re.fullmatch(r"[0-9a-fA-F]{64}", digest):
            raise ValueError(f"Invalid SHA-256 for runtime.{role} in '{manifest_path}'")
        expected_modules[role] = digest.lower()

    overrides = manifest.get("overrides")
    required_overrides = manifest.get("requiredOverrides")
    if not isinstance(overrides, dict) or not isinstance(required_overrides, list):
        raise ValueError(f"Invalid run manifest shader override section: {manifest_path}")
    for name, digest in overrides.items():
        if (
            not isinstance(name, str)
            or Path(name).name != name
            or not name.lower().endswith(".cso")
            or not isinstance(digest, str)
            or not re.fullmatch(r"[0-9a-fA-F]{64}", digest)
        ):
            raise ValueError(f"Invalid shader override entry in '{manifest_path}': {name}")
    if any(not isinstance(name, str) or name not in overrides for name in required_overrides):
        raise ValueError(f"Invalid requiredOverrides in '{manifest_path}'")

    return {
        "test_version": manifest["testVersion"],
        "manifest_path": str(manifest_path.resolve()),
        "expected_modules": expected_modules,
        "expected_overrides": {name: digest.lower() for name, digest in overrides.items()},
        "required_overrides": list(required_overrides),
    }


def is_rgba_non_black(rgba: Tuple[int, int, int, int]) -> bool:
    """
    Black/alpha semantics:
    Consistent with `try_inspect_complete_bmp` (where a pixel is non-black iff R != 0 or G != 0 or B != 0),
    an RGBA sample is considered non-black iff any visible color channel (R, G, or B) is non-zero.
    Alpha (channel index 3) alone does not make a pixel non-black (e.g. RGBA=(0,0,0,255) is opaque black),
    while `all_samples_zero` strictly reports whether all 4 channels `(R, G, B, A) == (0, 0, 0, 0)`.
    """
    return rgba[0] != 0 or rgba[1] != 0 or rgba[2] != 0


@dataclasses.dataclass
class DrawSample:
    draw_type: str
    vs: Optional[str] = None
    ps: Optional[str] = None
    elements: Optional[int] = None
    instances: Optional[int] = None
    rtv_slot: Optional[int] = None
    viewport: Optional[str] = None
    line_no: int = 0


@dataclasses.dataclass
class ResourceHistory:
    resource: str
    operations: int
    draws: int
    skipped: int
    clears: int
    line_no: int
    retained_draws: Deque[DrawSample] = dataclasses.field(
        default_factory=lambda: deque(maxlen=MAX_RETAINED_DRAWS_PER_RESOURCE)
    )

    def unique_vs(self) -> List[str]:
        return sorted({d.vs for d in self.retained_draws if d.vs})

    def unique_ps(self) -> List[str]:
        return sorted({d.ps for d in self.retained_draws if d.ps})

    def unique_viewports(self) -> List[str]:
        return sorted({d.viewport for d in self.retained_draws if d.viewport})

    def to_dict(self) -> Dict[str, Any]:
        return {
            "resource": self.resource,
            "operations": self.operations,
            "draws": self.draws,
            "skipped": self.skipped,
            "clears": self.clears,
            "line_no": self.line_no,
            "unique_vs": self.unique_vs(),
            "unique_ps": self.unique_ps(),
            "unique_viewports": self.unique_viewports(),
            "retained_draw_count": len(self.retained_draws),
            "retained_draws": [dataclasses.asdict(d) for d in self.retained_draws],
        }


@dataclasses.dataclass
class ReadbackRecord:
    kind: str  # "source" or "backbuffer"
    present: int
    line_no: int
    source: Optional[str] = None
    dimension: Optional[int] = None
    width: Optional[int] = None
    height: Optional[int] = None
    format: Optional[int] = None
    sequence: Optional[int] = None
    buffer: Optional[int] = None
    sample_pixels_rgba: List[Tuple[int, int, int, int]] = dataclasses.field(default_factory=list)
    non_black_count: Optional[int] = None
    total_sampled: Optional[int] = None
    non_black_pct: Optional[float] = None
    has_explicit_summary: bool = False
    emitted_complete_event: bool = False

    @property
    def all_samples_zero(self) -> Optional[bool]:
        if not self.sample_pixels_rgba:
            return None
        return all(rgba == (0, 0, 0, 0) for rgba in self.sample_pixels_rgba)

    @property
    def all_samples_rgb_black(self) -> Optional[bool]:
        if not self.sample_pixels_rgba:
            return None
        return all(not is_rgba_non_black(rgba) for rgba in self.sample_pixels_rgba)

    def recompute_from_samples_if_needed(self) -> None:
        """
        When the native log emits the 5-point RGBA samples without a `Non-black pixels: X/100`
        summary line, compute `non_black_count`, `total_sampled`, and `non_black_pct` directly
        from `sample_pixels_rgba` using RGB non-black semantics (R!=0 or G!=0 or B!=0).
        If an explicit `Non-black pixels:` line arrives later, it overrides these values.
        """
        if self.has_explicit_summary or not self.sample_pixels_rgba:
            return
        total = len(self.sample_pixels_rgba)
        nb = sum(1 for rgba in self.sample_pixels_rgba if is_rgba_non_black(rgba))
        self.non_black_count = nb
        self.total_sampled = total
        self.non_black_pct = round((nb * 100.0) / total, 1) if total > 0 else 0.0

    def is_complete(self) -> bool:
        return self.has_explicit_summary or len(self.sample_pixels_rgba) >= EXPECTED_POINT_SAMPLES


@dataclasses.dataclass
class ShaderStatus:
    shader_hash: str
    status: str  # "READY" or "PLACEHOLDER"
    stage: Optional[str] = None
    reason: Optional[str] = None
    occurrences: int = 1
    first_line: int = 0


@dataclasses.dataclass
class BmpInspection:
    label: str
    path: str
    file_size_bytes: int
    expected_min_bytes: int
    width: int
    height: int
    bits_per_pixel: int
    total_pixels: int
    non_black_pixels: int
    first_non_black_xy: Optional[Tuple[int, int]] = None
    first_non_black_rgb: Optional[Tuple[int, int, int]] = None


def try_inspect_complete_bmp(bmp_path: Path, label: str = "bmp") -> Optional[BmpInspection]:
    """
    Checks if `bmp_path` exists and has been completely written to disk by verifying its
    actual byte size against the BMP header's pixel offset + padded row stride * height.
    Returns `None` if the file does not exist yet or is still being flushed/written.
    Raises `ValueError` if the header is permanently malformed (non-BM or unsupported bpp).
    """
    if not bmp_path.is_file():
        return None
    try:
        actual_size = bmp_path.stat().st_size
        if actual_size < 54:
            return None  # Still writing header

        with bmp_path.open("rb") as f:
            header = f.read(54)
            if len(header) < 54:
                return None
            if header[:2] != b"BM":
                raise ValueError(f"Invalid BMP signature in {bmp_path}")

            declared_size = struct.unpack_from("<I", header, 2)[0]
            pixel_offset = struct.unpack_from("<I", header, 10)[0]
            width, height = struct.unpack_from("<ii", header, 18)
            bpp = struct.unpack_from("<H", header, 28)[0]
            compression = struct.unpack_from("<I", header, 30)[0]

            if compression not in (0, 3) or bpp not in (24, 32) or width == 0 or height == 0:
                raise ValueError(
                    f"Unsupported BMP geometry/format (w={width}, h={height}, bpp={bpp}, comp={compression})"
                )

            abs_w = abs(width)
            abs_h = abs(height)
            bytes_per_pixel = bpp // 8
            row_stride = ((abs_w * bytes_per_pixel + 3) // 4) * 4
            expected_min_size = pixel_offset + (row_stride * abs_h)
            if declared_size > expected_min_size:
                expected_min_size = declared_size

            if actual_size < expected_min_size:
                return None  # File is still incomplete on disk

            f.seek(pixel_offset)
            non_black = 0
            first_xy: Optional[Tuple[int, int]] = None
            first_rgb: Optional[Tuple[int, int, int]] = None

            for row_idx in range(abs_h):
                row = f.read(row_stride)
                if len(row) < abs_w * bytes_per_pixel:
                    return None
                y = (abs_h - 1 - row_idx) if height > 0 else row_idx
                limit = abs_w * bytes_per_pixel
                for col_byte in range(0, limit, bytes_per_pixel):
                    b_val = row[col_byte]
                    g_val = row[col_byte + 1]
                    r_val = row[col_byte + 2]
                    if b_val != 0 or g_val != 0 or r_val != 0:
                        non_black += 1
                        if first_xy is None:
                            x = col_byte // bytes_per_pixel
                            first_xy = (x, y)
                            first_rgb = (r_val, g_val, b_val)

            return BmpInspection(
                label=label,
                path=str(bmp_path),
                file_size_bytes=actual_size,
                expected_min_bytes=expected_min_size,
                width=abs_w,
                height=abs_h,
                bits_per_pixel=bpp,
                total_pixels=abs_w * abs_h,
                non_black_pixels=non_black,
                first_non_black_xy=first_xy,
                first_non_black_rgb=first_rgb,
            )
    except OSError:
        return None


class LiveMonitorState:
    """
    Bounded-memory state machine that ingests proxy log lines one by one and emits
    concise real-time diagnostic events.
    Correlates split/interleaved readback blocks by (kind, present) without losing state
    when unrelated shader/thread lines are logged in between.
    """

    def __init__(
        self,
        event_callback: Optional[Callable[[str, Dict[str, Any]], None]] = None,
        run_manifest: Optional[Dict[str, Any]] = None,
    ) -> None:
        self.event_callback = event_callback
        self.run_manifest = run_manifest
        self.module_fingerprint_observations: Dict[str, List[str]] = {}
        self.module_fingerprint_failures: Dict[str, str] = {}
        self.generation: int = 1
        self.truncation_count: int = 0
        self.lines_processed: int = 0

        self.resources_by_ptr: Dict[str, ResourceHistory] = {}
        self.current_history_resource: Optional[ResourceHistory] = None

        # Bounded maps of (kind, present) -> ReadbackRecord
        self.readbacks_by_key: "OrderedDict[Tuple[str, int], ReadbackRecord]" = OrderedDict()
        self.last_open_readback_by_kind: Dict[str, ReadbackRecord] = {}
        self.last_touched_readback: Optional[ReadbackRecord] = None

        self.latest_readback_src: Optional[ReadbackRecord] = None
        self.latest_readback_bb: Optional[ReadbackRecord] = None
        self.present100_readback_src: Optional[ReadbackRecord] = None
        self.present100_readback_bb: Optional[ReadbackRecord] = None

        self.shaders_ready: Dict[str, ShaderStatus] = {}
        self.shaders_placeholder: Dict[str, ShaderStatus] = {}
        self.total_ready_events: int = 0
        self.total_placeholder_events: int = 0

        self.copy_to_bb_recent: Deque[Dict[str, Any]] = deque(maxlen=MAX_RECENT_COPY_TO_BB)
        self.copy_to_bb_seq100: Optional[Dict[str, Any]] = None

        self.heartbeats: Deque[Dict[str, Any]] = deque(maxlen=MAX_HEARTBEATS)
        self.dispatch_clues: Deque[Dict[str, Any]] = deque(maxlen=MAX_DISPATCH_CLUES)
        self.dispatch_clue_total: int = 0

        self.bmp_results: Dict[str, BmpInspection] = {}
        self.recent_events: Deque[Dict[str, Any]] = deque(maxlen=MAX_LIVE_EVENTS)

    def _emit(self, event_type: str, summary: str, payload: Optional[Dict[str, Any]] = None) -> None:
        evt = {
            "generation": self.generation,
            "line_no": self.lines_processed,
            "event_type": event_type,
            "summary": summary,
            "payload": payload or {},
        }
        self.recent_events.append(evt)
        if self.event_callback:
            self.event_callback(summary, evt)

    def on_log_truncated_or_rotated(self, reason: str) -> None:
        """Resets per-run transient parse cursors while recording the rotation/truncation."""
        self.truncation_count += 1
        self.generation += 1
        self.lines_processed = 0
        self.resources_by_ptr.clear()
        self.current_history_resource = None
        self.readbacks_by_key.clear()
        self.last_open_readback_by_kind.clear()
        self.last_touched_readback = None
        self.latest_readback_src = None
        self.latest_readback_bb = None
        self.present100_readback_src = None
        self.present100_readback_bb = None
        self.shaders_ready.clear()
        self.shaders_placeholder.clear()
        self.module_fingerprint_observations.clear()
        self.module_fingerprint_failures.clear()
        self.total_ready_events = 0
        self.total_placeholder_events = 0
        self.copy_to_bb_recent.clear()
        self.copy_to_bb_seq100 = None
        self.heartbeats.clear()
        self.dispatch_clues.clear()
        self.dispatch_clue_total = 0
        self._emit("LOG_RESET", f"[GEN #{self.generation}] Log reset detected ({reason}); state cleared.")

    def _get_or_create_readback(self, kind: str, present_num: int, line_no: int) -> ReadbackRecord:
        key = (kind, present_num)
        if key in self.readbacks_by_key:
            rb = self.readbacks_by_key[key]
            self.readbacks_by_key.move_to_end(key)
        else:
            if len(self.readbacks_by_key) >= MAX_TRACKED_READBACK_PRESENTS:
                # Evict oldest non-Present-100 entry first so Present #100 is never lost
                evict_key = next(
                    (k for k in self.readbacks_by_key if k[1] != 100),
                    next(iter(self.readbacks_by_key)),
                )
                del self.readbacks_by_key[evict_key]
            rb = ReadbackRecord(kind=kind, present=present_num, line_no=line_no)
            self.readbacks_by_key[key] = rb

        self.last_open_readback_by_kind[kind] = rb
        self.last_touched_readback = rb
        if kind == "source":
            self.latest_readback_src = rb
            if present_num == 100:
                self.present100_readback_src = rb
        else:
            self.latest_readback_bb = rb
            if present_num == 100:
                self.present100_readback_bb = rb
        return rb

    def _merge_readback_header_fields(self, rb: ReadbackRecord, tail: str) -> None:
        m_src = RE_SOURCE_PTR.search(tail)
        if m_src:
            rb.source = norm_ptr(m_src.group(1))
        kv = {k.lower(): int(v) for k, v in RE_KEY_INT.findall(tail)}
        if "dimension" in kv:
            rb.dimension = kv["dimension"]
        if "width" in kv:
            rb.width = kv["width"]
        if "height" in kv:
            rb.height = kv["height"]
        if "format" in kv:
            rb.format = kv["format"]
        if "sequence" in kv:
            rb.sequence = kv["sequence"]
        if "buffer" in kv:
            rb.buffer = kv["buffer"]
        m_sz = RE_SIZE_WH.search(tail)
        if m_sz:
            rb.width = int(m_sz.group(1))
            rb.height = int(m_sz.group(2))

    def _resolve_target_readback_for_sample_line(self, line: str) -> Optional[ReadbackRecord]:
        """
        Determines which open ReadbackRecord should receive a sample/summary continuation line.
        - If the line carries an explicit `[READBACK_SRC]` or `[READBACK]` prefix, routes to
          the latest open record of that specific kind.
        - Otherwise routes to `last_touched_readback` if it is still collecting samples/summary,
          or falls back to any open incomplete readback record.
        """
        m_tag = RE_READBACK_TAG_PREFIX.match(line)
        if m_tag:
            tag = m_tag.group(1).upper()
            kind = "source" if tag == "READBACK_SRC" else "backbuffer"
            return self.last_open_readback_by_kind.get(kind)

        if self.last_touched_readback is not None:
            # Prefer last_touched_readback if it doesn't yet have all 5 samples or explicit summary
            if (
                len(self.last_touched_readback.sample_pixels_rgba) < EXPECTED_POINT_SAMPLES
                or not self.last_touched_readback.has_explicit_summary
            ):
                return self.last_touched_readback

        # Fallback: check most recently updated readbacks in reverse order
        for rb in reversed(list(self.readbacks_by_key.values())):
            if len(rb.sample_pixels_rgba) < EXPECTED_POINT_SAMPLES or not rb.has_explicit_summary:
                return rb
        return self.last_touched_readback

    def feed_line(self, raw_line: str) -> None:
        self.lines_processed += 1
        line_no = self.lines_processed
        line = raw_line.strip()
        if not line:
            return

        fingerprint = RE_MODULE_FINGERPRINT.search(line)
        if fingerprint:
            role = "d3d12" if fingerprint.group(1).lower() == "d3d12" else "dxgi"
            digest = fingerprint.group(2).lower()
            observations = self.module_fingerprint_observations.setdefault(role, [])
            if digest not in observations and len(observations) < 4:
                observations.append(digest)
            expected = (
                self.run_manifest["expected_modules"].get(role)
                if self.run_manifest else None
            )
            status = "matches" if expected == digest else "mismatch" if expected else "observed"
            self._emit(
                "MODULE_FINGERPRINT",
                f"[MODULE_FINGERPRINT] {role}={digest} status={status}",
                {"role": role, "sha256": digest, "expected_sha256": expected, "status": status},
            )
            return

        fingerprint_failure = RE_MODULE_FINGERPRINT_FAILED.search(line)
        if fingerprint_failure:
            role = "d3d12" if fingerprint_failure.group(1).lower() == "d3d12" else "dxgi"
            reason = fingerprint_failure.group(2)
            self.module_fingerprint_failures[role] = reason
            self._emit(
                "MODULE_FINGERPRINT_FAILED",
                f"[MODULE_FINGERPRINT_FAILED] {role} reason={reason}",
                {"role": role, "reason": reason},
            )
            return

        # 1. Resource History header line
        m_hist = RE_HISTORY_RESOURCE.search(line)
        if m_hist:
            ptr = norm_ptr(m_hist.group(1))
            if ptr not in self.resources_by_ptr and len(self.resources_by_ptr) >= MAX_TRACKED_RESOURCES:
                oldest_key = next(iter(self.resources_by_ptr))
                del self.resources_by_ptr[oldest_key]
            rec = ResourceHistory(
                resource=ptr,
                operations=int(m_hist.group(2)),
                draws=int(m_hist.group(3)),
                skipped=int(m_hist.group(4)),
                clears=int(m_hist.group(5)),
                line_no=line_no,
            )
            self.resources_by_ptr[ptr] = rec
            self.current_history_resource = rec
            self._emit(
                "SOURCE_HISTORY",
                f"[HISTORY] Resource {ptr}: operations={rec.operations} draws={rec.draws} "
                f"skipped={rec.skipped} clears={rec.clears}",
                {"resource": ptr, "draws": rec.draws, "skipped": rec.skipped, "clears": rec.clears},
            )
            return

        # 2. [READBACK_SRC] Present #N header (merges split `CopyResource source=...` and `size=WxH...` headers)
        m_rb_src = RE_READBACK_SRC_HEADER.search(line)
        if m_rb_src:
            present_num = int(m_rb_src.group(1))
            tail = m_rb_src.group(2)
            rb = self._get_or_create_readback("source", present_num, line_no)
            self._merge_readback_header_fields(rb, tail)
            self.current_history_resource = None
            self._extract_readback_continuation(line, rb)
            return

        # 3. [READBACK] Present #N (backbuffer) header
        m_rb_bb = RE_READBACK_BB_HEADER.search(line)
        if m_rb_bb:
            present_num = int(m_rb_bb.group(1))
            tail = m_rb_bb.group(2)
            rb = self._get_or_create_readback("backbuffer", present_num, line_no)
            self._merge_readback_header_fields(rb, tail)
            self.current_history_resource = None
            self._extract_readback_continuation(line, rb)
            return

        # 4. Readback sample / summary continuation lines (even if interleaved with other log lines)
        if RE_NON_BLACK.search(line) or RE_RGBA_TUPLE.search(line):
            target_rb = self._resolve_target_readback_for_sample_line(line)
            if target_rb is not None and self._extract_readback_continuation(line, target_rb):
                return

        # 5. Retained draw samples under current resource history
        if self.current_history_resource is not None:
            m_draw = RE_API_DRAW_ENTRY.search(line) or RE_DRAW_ENTRY.search(line)
            if m_draw and (RE_VS_HASH.search(line) or RE_PS_HASH.search(line)):
                vs_m = RE_VS_HASH.search(line)
                ps_m = RE_PS_HASH.search(line)
                el_m = RE_ELEMENTS.search(line)
                in_m = RE_INSTANCES.search(line)
                rtv_m = RE_RTV_SLOT.search(line)
                vp_m = RE_VIEWPORT.search(line)
                vp_str = None
                if vp_m:
                    vp_width = vp_m.group("width") or vp_m.group("tuple_width")
                    vp_height = vp_m.group("height") or vp_m.group("tuple_height")
                    vp_str = f"{vp_width.split('.')[0]}x{vp_height.split('.')[0]}"
                self.current_history_resource.retained_draws.append(
                    DrawSample(
                        draw_type=m_draw.group(1),
                        vs=norm_hash(vs_m.group(1)) if vs_m else None,
                        ps=norm_hash(ps_m.group(1)) if ps_m else None,
                        elements=int(el_m.group(1)) if el_m else None,
                        instances=int(in_m.group(1)) if in_m else None,
                        rtv_slot=int(rtv_m.group(1)) if rtv_m else None,
                        viewport=vp_str,
                        line_no=line_no,
                    )
                )
                return

        # 6. Shader Ready / Shader Placeholder (interleaved lines do NOT clear open readbacks)
        if "SHADER" in line.upper():
            if RE_SHADER_READY.search(line):
                self._record_shader(line, line_no, "READY", self.shaders_ready)
                return
            if RE_SHADER_PLACEHOLDER.search(line):
                self._record_shader(line, line_no, "PLACEHOLDER", self.shaders_placeholder)
                return

        # 7. COPY_TO_BB
        if "COPY_TO_BB" in line.upper():
            m_copy = RE_COPY_TO_BB.search(line)
            if m_copy:
                src_ptr = norm_ptr(m_copy.group(1))
                tail = m_copy.group(2)
                seq_m = RE_SEQ.search(line)
                seq_val = int(seq_m.group(1)) if seq_m else None
                kv = {k.lower(): int(v) for k, v in RE_KEY_INT.findall(tail)}
                entry = {
                    "line_no": line_no,
                    "source": src_ptr,
                    "sequence": seq_val,
                    **{k: v for k, v in kv.items() if k != "sequence"},
                }
                self.copy_to_bb_recent.append(entry)
                if seq_val == 100:
                    self.copy_to_bb_seq100 = entry
                    self._emit(
                        "COPY_TO_BB_100",
                        f"[COPY_TO_BB] sequence=100 source={src_ptr}",
                        entry,
                    )
                return

        # 8. Heartbeat counters
        if "present=" in line.lower() and "skip-vs=" in line.lower():
            m_hb = RE_HEARTBEAT.search(line)
            if m_hb:
                metrics = {k.lower(): int(v) for k, v in RE_HB_KV.findall(line)}
                hb_entry = {"line_no": line_no, "metrics": metrics}
                self.heartbeats.append(hb_entry)
                hb_summary = ", ".join(f"{k}={v}" for k, v in metrics.items())
                self._emit("HEARTBEAT", f"[HEARTBEAT] {hb_summary}", hb_entry)
                return

        # 9. Compute dispatch skip clues
        if RE_DISPATCH_CLUE.search(line):
            self.dispatch_clue_total += 1
            clue = {"line_no": line_no, "text": line[:200]}
            self.dispatch_clues.append(clue)
            if self.dispatch_clue_total <= 5:
                self._emit("DISPATCH_CLUE", f"[DISPATCH_CLUE] line {line_no}: {line[:120]}", clue)

    def _extract_readback_continuation(self, line: str, rb: ReadbackRecord) -> bool:
        matched = False
        m_nb = RE_NON_BLACK.search(line)
        if m_nb:
            rb.non_black_count = int(m_nb.group(1))
            rb.total_sampled = int(m_nb.group(2))
            rb.non_black_pct = float(m_nb.group(3))
            rb.has_explicit_summary = True
            matched = True

        for m_rgba in RE_RGBA_TUPLE.finditer(line):
            groups = m_rgba.groups()
            if groups[0] is not None:
                rgba = (int(groups[0]), int(groups[1]), int(groups[2]), int(groups[3]))
            elif groups[4] is not None:
                rgba = (int(groups[4]), int(groups[5]), int(groups[6]), int(groups[7]))
            else:
                rgba = (int(groups[8]), int(groups[9]), int(groups[10]), int(groups[11]))
            if len(rb.sample_pixels_rgba) < 16:
                rb.sample_pixels_rgba.append(rgba)
            matched = True

        if matched:
            rb.recompute_from_samples_if_needed()

        if rb.is_complete() and not rb.emitted_complete_event:
            rb.emitted_complete_event = True
            self._emit_readback_complete(rb)

        return matched

    def _emit_readback_complete(self, rb: ReadbackRecord) -> None:
        rb.recompute_from_samples_if_needed()
        if rb.kind == "source":
            matched_hist = self.resources_by_ptr.get(rb.source or "")
            corr = (
                f"matches RTV history (draws={matched_hist.draws}, skipped={matched_hist.skipped}, clears={matched_hist.clears})"
                if matched_hist
                else "no matching RTV history key yet"
            )
            self._emit(
                "READBACK_SRC",
                f"[READBACK_SRC] Present #{rb.present} source={rb.source} "
                f"non-black={rb.non_black_count}/{rb.total_sampled} ({rb.non_black_pct}%) "
                f"all_samples_zero={rb.all_samples_zero} [{corr}]",
                {
                    "present": rb.present,
                    "source": rb.source,
                    "non_black_count": rb.non_black_count,
                    "total_sampled": rb.total_sampled,
                    "all_samples_zero": rb.all_samples_zero,
                },
            )
        else:
            self._emit(
                "READBACK_BB",
                f"[READBACK_BB] Present #{rb.present} buffer={rb.buffer} "
                f"non-black={rb.non_black_count}/{rb.total_sampled} ({rb.non_black_pct}%) "
                f"all_samples_zero={rb.all_samples_zero}",
                {
                    "present": rb.present,
                    "buffer": rb.buffer,
                    "non_black_count": rb.non_black_count,
                    "total_sampled": rb.total_sampled,
                    "all_samples_zero": rb.all_samples_zero,
                },
            )

    def _record_shader(
        self,
        line: str,
        line_no: int,
        status: str,
        table: Dict[str, ShaderStatus],
    ) -> None:
        if status == "READY":
            self.total_ready_events += 1
        else:
            self.total_placeholder_events += 1

        hashes = [norm_hash(h) for h in RE_ANY_HASH16.findall(line)]
        if not hashes:
            return
        shader_hash = hashes[0]
        stage_m = RE_SHADER_STAGE.search(line)
        stage_val = None
        if stage_m:
            raw_stage = (stage_m.group(1) or stage_m.group(2) or "").upper()
            stage_map = {"VERTEX": "VS", "PIXEL": "PS", "COMPUTE": "CS"}
            stage_val = stage_map.get(raw_stage, raw_stage)
        reason_m = RE_REASON.search(line)
        reason_val = reason_m.group(1) if reason_m else None

        if shader_hash in table:
            existing = table[shader_hash]
            existing.occurrences += 1
            if not existing.stage and stage_val:
                existing.stage = stage_val
            if not existing.reason and reason_val:
                existing.reason = reason_val
        else:
            if len(table) >= MAX_TRACKED_SHADERS:
                oldest = next(iter(table))
                del table[oldest]
            rec = ShaderStatus(
                shader_hash=shader_hash,
                status=status,
                stage=stage_val,
                reason=reason_val,
                occurrences=1,
                first_line=line_no,
            )
            table[shader_hash] = rec
            if status == "PLACEHOLDER":
                self._emit(
                    "SHADER_PLACEHOLDER",
                    f"[SHADER_PLACEHOLDER] hash={shader_hash} stage={stage_val or '?'} reason={reason_val or '?'}",
                    dataclasses.asdict(rec),
                )
            else:
                self._emit(
                    "SHADER_READY",
                    f"[SHADER_READY] hash={shader_hash} stage={stage_val or '?'}",
                    dataclasses.asdict(rec),
                )

    def record_bmp_inspection(self, info: BmpInspection) -> None:
        self.bmp_results[info.label] = info
        self._emit(
            "BMP_COMPLETE",
            f"[BMP:{info.label.upper()}] {Path(info.path).name} complete ({info.width}x{info.height}, "
            f"{info.bits_per_pixel}bpp): non-black={info.non_black_pixels}/{info.total_pixels}",
            dataclasses.asdict(info),
        )

    def present_reached(self, present_num: int) -> bool:
        source = self.readbacks_by_key.get(("source", present_num))
        backbuffer = self.readbacks_by_key.get(("backbuffer", present_num))
        return bool(
            source and source.emitted_complete_event
            and backbuffer and backbuffer.emitted_complete_event
        )

    def present100_reached(self) -> bool:
        return self.present_reached(100)

    def run_identity_report(self) -> Dict[str, Any]:
        if not self.run_manifest:
            attestation_status = "untracked"
            expected_modules: Dict[str, str] = {}
            test_version = None
            manifest_path = None
        else:
            expected_modules = self.run_manifest["expected_modules"]
            test_version = self.run_manifest["test_version"]
            manifest_path = self.run_manifest["manifest_path"]
            has_mismatch = any(
                observed != expected_modules[role]
                for role, digests in self.module_fingerprint_observations.items()
                for observed in digests
                if role in expected_modules
            )
            has_all_fingerprints = all(
                expected in self.module_fingerprint_observations.get(role, [])
                for role, expected in expected_modules.items()
            )
            if self.module_fingerprint_failures:
                attestation_status = "failed"
            elif has_mismatch:
                attestation_status = "mismatch"
            elif has_all_fingerprints:
                attestation_status = "verified"
            else:
                attestation_status = "pending"

        return {
            "test_version": test_version,
            "manifest_path": manifest_path,
            "expected_modules": expected_modules,
            "observed_modules": {
                role: digests[-1] for role, digests in self.module_fingerprint_observations.items()
                if digests
            },
            "module_fingerprint_observations": {
                role: list(digests)
                for role, digests in self.module_fingerprint_observations.items()
            },
            "module_fingerprint_failures": dict(self.module_fingerprint_failures),
            "attestation_status": attestation_status,
        }

    def build_report(self, log_path: str, capture_present: int = 100) -> Dict[str, Any]:
        # Strictly report Present #100 in `present_100` (never substitute Present #200+ as #100)
        rb_src = self.present100_readback_src
        rb_bb = self.present100_readback_bb
        if rb_src is not None:
            rb_src.recompute_from_samples_if_needed()
        if rb_bb is not None:
            rb_bb.recompute_from_samples_if_needed()

        matched_hist = self.resources_by_ptr.get(rb_src.source) if (rb_src and rb_src.source) else None
        capture_src = self.readbacks_by_key.get(("source", capture_present))
        capture_bb = self.readbacks_by_key.get(("backbuffer", capture_present))
        if capture_src is not None:
            capture_src.recompute_from_samples_if_needed()
        if capture_bb is not None:
            capture_bb.recompute_from_samples_if_needed()

        history_vs = sorted({vs for r in self.resources_by_ptr.values() for vs in r.unique_vs()})
        history_ps = sorted({ps for r in self.resources_by_ptr.values() for ps in r.unique_ps()})
        target_hashes = history_vs + history_ps

        verified_facts: List[str] = []
        hypotheses: List[str] = []

        if rb_src and matched_hist:
            verified_facts.append(
                f"Direct pointer identity verified at Present #{rb_src.present}: [READBACK_SRC] "
                f"source={rb_src.source} matches Resource History key {matched_hist.resource} "
                f"(operations={matched_hist.operations}, draws={matched_hist.draws}, "
                f"skipped={matched_hist.skipped}, clears={matched_hist.clears})."
            )
        elif self.resources_by_ptr and not rb_src:
            verified_facts.append(
                f"Recorded {len(self.resources_by_ptr)} RTV source history resource(s); no Present #100 [READBACK_SRC] observed."
            )

        if matched_hist:
            verified_facts.append(
                f"Source RTV {matched_hist.resource} executed {matched_hist.draws} draw call(s) "
                f"(skipped={matched_hist.skipped}, clears={matched_hist.clears}) with VS "
                f"[{', '.join(matched_hist.unique_vs()) or 'none'}] and PS "
                f"[{', '.join(matched_hist.unique_ps()) or 'none'}] at viewport "
                f"[{', '.join(matched_hist.unique_viewports()) or 'unspecified'}]."
            )

        if rb_src:
            verified_facts.append(
                f"[READBACK_SRC] Present #{rb_src.present} (source={rb_src.source}, "
                f"{rb_src.width}x{rb_src.height}, fmt={rb_src.format}, seq={rb_src.sequence}): "
                f"Non-black pixels={rb_src.non_black_count}/{rb_src.total_sampled} ({rb_src.non_black_pct}%), "
                f"5-point samples all_zero={rb_src.all_samples_zero}."
            )

        if rb_bb:
            verified_facts.append(
                f"[READBACK] Backbuffer Present #{rb_bb.present} (buffer={rb_bb.buffer}, "
                f"{rb_bb.width}x{rb_bb.height}, fmt={rb_bb.format}): "
                f"Non-black pixels={rb_bb.non_black_count}/{rb_bb.total_sampled} ({rb_bb.non_black_pct}%), "
                f"5-point samples all_zero={rb_bb.all_samples_zero}."
            )

        for label, bmp in self.bmp_results.items():
            verified_facts.append(
                f"BMP '{label}' ({Path(bmp.path).name}): {bmp.non_black_pixels}/{bmp.total_pixels} "
                f"non-black pixels ({bmp.width}x{bmp.height}, {bmp.bits_per_pixel}-bit)."
            )

        ready_targets = [h for h in target_hashes if h in self.shaders_ready]
        if ready_targets:
            verified_facts.append(
                f"Target graphics shaders in RTV history logged SHADER READY: {', '.join(ready_targets)}."
            )

        if self.shaders_placeholder:
            ph_list = [
                f"{s.shader_hash} (stage={s.stage or 'unknown'}, reason={s.reason or 'unspecified'})"
                for s in self.shaders_placeholder.values()
            ]
            verified_facts.append(
                f"{len(self.shaders_placeholder)} unique shader(s) logged SHADER PLACEHOLDER: {'; '.join(ph_list)}."
            )

        if self.heartbeats:
            last_hb = self.heartbeats[-1]["metrics"]
            hb_str = ", ".join(f"{k}={v}" for k, v in last_hb.items())
            verified_facts.append(f"Latest heartbeat (line {self.heartbeats[-1]['line_no']}): {hb_str}.")

        # Disciplined diagnostic boundaries (never over-claim from black pixels or placeholders)
        if rb_src and rb_src.non_black_count == 0:
            hypotheses.append(
                "BOUNDARY (CopyResource): Because [READBACK_SRC] is already 100% black (all zeros) before "
                "CopyResource, a 100% black backbuffer in [READBACK] does NOT validate CopyResource transfer "
                "fidelity (copying zeros onto zeros is indistinguishable from a no-op). It DOES prove the "
                "black output originates upstream on the source RTV prior to CopyResource."
            )
            hypotheses.append(
                "HYPOTHESIS (Zero fragment output / black inputs): Executed RTV draw calls prove command "
                "invocation, not non-zero pixel writes. The PS overrides may be reading unbound/black SRVs "
                "(t0..tn), zeroed Constant Buffers (b0..bn), or the second alternating draw may overwrite the "
                "first draw's output with black."
            )

        if self.shaders_placeholder:
            ph_hashes = ", ".join(self.shaders_placeholder.keys())
            hypotheses.append(
                f"BOUNDARY (Compute placeholders [{ph_hashes}]): SHADER PLACEHOLDER proves missing usable SM5 "
                "bytecode for those hashes, but does NOT by itself prove whether their Dispatch calls were "
                "skipped at runtime or whether their outputs feed the Present #100 source RTV draws."
            )

        return {
            "schema": SCHEMA_ID,
            "schema_version": SCHEMA_VERSION,
            "run_identity": self.run_identity_report(),
            "monitor_metadata": {
                "log_path": log_path,
                "generation": self.generation,
                "truncation_count": self.truncation_count,
                "lines_processed_in_generation": self.lines_processed,
                "capture_present": capture_present,
            },
            "capture_target": {
                "present": capture_present,
                "complete": self.present_reached(capture_present),
                "source_readback": dataclasses.asdict(capture_src) if capture_src else None,
                "backbuffer_readback": dataclasses.asdict(capture_bb) if capture_bb else None,
            },
            "present_100": {
                "source_readback": dataclasses.asdict(rb_src) if rb_src else None,
                "backbuffer_readback": dataclasses.asdict(rb_bb) if rb_bb else None,
                "matched_source_history": matched_hist.to_dict() if matched_hist else None,
                "all_source_histories": {k: v.to_dict() for k, v in self.resources_by_ptr.items()},
                "copy_to_bb_seq100": self.copy_to_bb_seq100,
                "copy_to_bb_recent": list(self.copy_to_bb_recent),
            },
            "shaders": {
                "target_graphics_hashes_in_history": {"vs": history_vs, "ps": history_ps},
                "ready_unique_count": len(self.shaders_ready),
                "ready_total_events": self.total_ready_events,
                "ready": {k: dataclasses.asdict(v) for k, v in self.shaders_ready.items()},
                "placeholder_unique_count": len(self.shaders_placeholder),
                "placeholder_total_events": self.total_placeholder_events,
                "placeholders": {k: dataclasses.asdict(v) for k, v in self.shaders_placeholder.items()},
            },
            "runtime_clues": {
                "heartbeats": list(self.heartbeats),
                "dispatch_clue_total": self.dispatch_clue_total,
                "dispatch_clues_retained": list(self.dispatch_clues),
            },
            "bmp_verification": {k: dataclasses.asdict(v) for k, v in self.bmp_results.items()},
            "verified_facts": verified_facts,
            "hypotheses_and_open_questions": hypotheses,
        }


class LogFileTailer:
    """
    Incremental, rotation/truncation-safe file tailer that buffers partial lines
    until a newline arrives.
    """

    def __init__(self, log_path: Path, state: LiveMonitorState, start_at_end: bool = False) -> None:
        self.log_path = log_path
        self.state = state
        self.offset: int = 0
        self.partial_buffer: str = ""
        self.file_id: Optional[Tuple[int, int]] = None
        self.initialized: bool = False
        self.start_at_end = start_at_end

    def poll(self) -> int:
        """
        Reads any newly appended complete lines from `self.log_path`.
        Detects file creation, truncation (`size < offset`), or file replacement.
        Returns the number of complete lines fed in this poll cycle.
        """
        if not self.log_path.is_file():
            return 0

        try:
            st = self.log_path.stat()
        except OSError:
            return 0

        current_id = (st.st_dev, st.st_ino)
        current_size = st.st_size

        if not self.initialized:
            self.initialized = True
            self.file_id = current_id
            if self.start_at_end:
                self.offset = current_size
                return 0
        else:
            if current_size < self.offset:
                self.offset = 0
                self.partial_buffer = ""
                self.file_id = current_id
                self.state.on_log_truncated_or_rotated(
                    f"file size shrank to {current_size} bytes"
                )
            elif self.file_id is not None and current_id != (0, 0) and current_id != self.file_id and current_size == 0:
                self.offset = 0
                self.partial_buffer = ""
                self.file_id = current_id
                self.state.on_log_truncated_or_rotated("file recreated")

        if current_size <= self.offset:
            return 0

        lines_fed = 0
        try:
            with self.log_path.open("rb") as f:
                f.seek(self.offset)
                # Read in 64 KB chunks to keep memory strictly bounded even on 30+ MB files
                while True:
                    chunk = f.read(65536)
                    if not chunk:
                        break
                    self.offset += len(chunk)
                    text = self.partial_buffer + chunk.decode("utf-8", errors="replace")
                    parts = text.split("\n")
                    self.partial_buffer = parts.pop()  # Remainder after last '\n'
                    for line in parts:
                        self.state.feed_line(line.rstrip("\r"))
                        lines_fed += 1
        except OSError:
            pass

        return lines_fed

    def flush_partial(self) -> int:
        """Flushes any trailing non-newline-terminated line on shutdown/EOF."""
        if self.partial_buffer.strip():
            line = self.partial_buffer.rstrip("\r")
            self.partial_buffer = ""
            self.state.feed_line(line)
            return 1
        return 0


def format_human_summary(report: Dict[str, Any]) -> str:
    meta = report["monitor_metadata"]
    p100 = report["present_100"]
    shaders = report["shaders"]
    clues = report["runtime_clues"]
    bmps = report["bmp_verification"]

    out: List[str] = []
    out.append("=" * 78)
    out.append(f"HELA LIVE FRAME MONITOR SUMMARY ({report['schema']} v{report['schema_version']})")
    out.append("=" * 78)
    out.append(
        f"Log Path   : {meta['log_path']} (gen={meta['generation']}, "
        f"truncations={meta['truncation_count']}, lines={meta['lines_processed_in_generation']:,})"
    )
    run_identity = report["run_identity"]
    out.append(
        f"Run Version: {run_identity['test_version'] or '<untracked>'} "
        f"(module fingerprints={run_identity['attestation_status']})"
    )
    out.append(
        f"Capture    : Present #{report['capture_target']['present']} "
        f"complete={report['capture_target']['complete']}"
    )
    out.append("")
    if run_identity["expected_modules"]:
        out.append("[0] RUNTIME PATCH IDENTITY")
        out.append("-" * 78)
        for role in ("d3d12", "dxgi"):
            expected = run_identity["expected_modules"].get(role, "<not pinned>")
            observed = run_identity["observed_modules"].get(role, "<not observed>")
            out.append(f"  {role:10}: expected={expected} observed={observed}")
        out.append("")
    out.append("[1] PRESENT #100 SOURCE HISTORY & READBACK CORRELATION")
    out.append("-" * 78)
    rb_src = p100["source_readback"]
    rb_bb = p100["backbuffer_readback"]
    matched = p100["matched_source_history"]

    if rb_src:
        out.append(
            f"  READBACK_SRC : Present #{rb_src['present']} | source={rb_src['source']} | "
            f"{rb_src['width']}x{rb_src['height']} fmt={rb_src['format']} seq={rb_src['sequence']}"
        )
        out.append(
            f"                 Non-black={rb_src['non_black_count']}/{rb_src['total_sampled']} "
            f"({rb_src['non_black_pct']}%) | samples={rb_src['sample_pixels_rgba']}"
        )
    else:
        out.append("  READBACK_SRC : <not observed>")

    if rb_bb:
        out.append(
            f"  READBACK_BB  : Present #{rb_bb['present']} | buffer={rb_bb['buffer']} | "
            f"{rb_bb['width']}x{rb_bb['height']} fmt={rb_bb['format']}"
        )
        out.append(
            f"                 Non-black={rb_bb['non_black_count']}/{rb_bb['total_sampled']} "
            f"({rb_bb['non_black_pct']}%) | samples={rb_bb['sample_pixels_rgba']}"
        )
    else:
        out.append("  READBACK_BB  : <not observed>")

    if matched:
        out.append(
            f"  MATCHED RTV  : Resource {matched['resource']} "
            f"(operations={matched['operations']} draws={matched['draws']} "
            f"skipped={matched['skipped']} clears={matched['clears']})"
        )
        out.append(
            f"                 VS={matched['unique_vs']} PS={matched['unique_ps']} "
            f"viewport={matched['unique_viewports']}"
        )
    elif p100["all_source_histories"]:
        out.append(f"  RTV HISTORY  : {len(p100['all_source_histories'])} resource(s) tracked:")
        for ptr, h in p100["all_source_histories"].items():
            out.append(
                f"                 - {ptr}: ops={h['operations']} draws={h['draws']} "
                f"skipped={h['skipped']} clears={h['clears']}"
            )
    else:
        out.append("  RTV HISTORY  : <none observed>")

    if bmps:
        out.append("  BMP CHECKS   :")
        for label, b in bmps.items():
            out.append(
                f"                 - [{label}] {b['path']}: {b['width']}x{b['height']} "
                f"({b['bits_per_pixel']}bpp) non-black={b['non_black_pixels']}/{b['total_pixels']}"
            )
    capture = report["capture_target"]
    if capture["present"] != 100:
        out.append(f"  CAPTURE #{capture['present']} complete={capture['complete']}")
        for label in ("source_readback", "backbuffer_readback"):
            record = capture[label]
            if record is None:
                out.append(f"                 - {label}: <not observed>")
            else:
                out.append(
                    f"                 - {label}: {record['width']}x{record['height']} "
                    f"non-black={record['non_black_count']}/{record['total_sampled']}"
                )
    out.append("")

    out.append("[2] SHADER READINESS & COMPUTE PLACEHOLDERS")
    out.append("-" * 78)
    out.append(
        f"  SHADER READY : {shaders['ready_unique_count']} unique "
        f"({shaders['ready_total_events']} total events)"
    )
    for h, s in shaders["ready"].items():
        out.append(f"                 - {h} (stage={s['stage'] or '?'}, count={s['occurrences']})")
    out.append(
        f"  PLACEHOLDERS : {shaders['placeholder_unique_count']} unique "
        f"({shaders['placeholder_total_events']} total events)"
    )
    for h, s in shaders["placeholders"].items():
        out.append(
            f"                 - {h} (stage={s['stage'] or '?'}, reason={s['reason'] or '?'}, "
            f"count={s['occurrences']})"
        )
    out.append("")

    out.append("[3] HEARTBEATS & DISPATCH CLUES")
    out.append("-" * 78)
    if clues["heartbeats"]:
        last_hb = clues["heartbeats"][-1]
        hb_str = ", ".join(f"{k}={v}" for k, v in last_hb["metrics"].items())
        out.append(f"  Last Heartbeat (line {last_hb['line_no']}): {hb_str}")
    else:
        out.append("  Heartbeats   : <none observed>")
    out.append(f"  Dispatch Clues Observed: {clues['dispatch_clue_total']}")
    out.append("")

    out.append("[4] VERIFIED FACTS (DIRECTLY SUPPORTED BY LOG/BMP EVIDENCE)")
    out.append("-" * 78)
    for idx, fact in enumerate(report["verified_facts"], start=1):
        out.append(f"  {idx}. {fact}")
    if not report["verified_facts"]:
        out.append("  <No forensic markers observed>")
    out.append("")

    out.append("[5] DIAGNOSTIC BOUNDARIES & OPEN HYPOTHESES")
    out.append("-" * 78)
    for idx, hyp in enumerate(report["hypotheses_and_open_questions"], start=1):
        out.append(f"  {idx}. {hyp}")
    if not report["hypotheses_and_open_questions"]:
        out.append("  <None>")
    out.append("=" * 78)
    return "\n".join(out)


def run_monitor(
    log_path: Path,
    poll_interval_sec: float = 0.20,
    once: bool = False,
    stop_after_present100: bool = False,
    stop_after_present_num: Optional[int] = None,
    max_wait_sec: Optional[float] = None,
    source_bmp_path: Optional[Path] = None,
    backbuffer_bmp_path: Optional[Path] = None,
    bmp_wait_timeout_sec: float = 5.0,
    json_path: Optional[Path] = None,
    quiet_events: bool = False,
    run_manifest_path: Optional[Path] = None,
) -> Tuple[int, Dict[str, Any]]:
    def _print_live_event(summary: str, _evt: Dict[str, Any]) -> None:
        if not quiet_events:
            print(f"[LIVE] {summary}", flush=True)

    if stop_after_present100:
        if stop_after_present_num is not None and stop_after_present_num != 100:
            print("ERROR: --stop-after-present100 conflicts with --stop-after-present.", file=sys.stderr)
            return 2, LiveMonitorState().build_report(str(log_path))
        stop_after_present_num = 100
    if stop_after_present_num is not None and (
        stop_after_present_num < 100 or stop_after_present_num % 100 != 0
    ):
        print("ERROR: stop-after-present must be a positive multiple of 100.", file=sys.stderr)
        return 2, LiveMonitorState().build_report(str(log_path))

    run_manifest = None
    if run_manifest_path is not None:
        try:
            run_manifest = load_run_manifest(run_manifest_path)
        except ValueError as exc:
            print(f"ERROR: {exc}", file=sys.stderr)
            return 2, LiveMonitorState().build_report(str(log_path))

    state = LiveMonitorState(
        event_callback=_print_live_event,
        run_manifest=run_manifest,
    )
    capture_present = stop_after_present_num or 100
    tailer = LogFileTailer(log_path, state=state, start_at_end=False)

    stop_requested = False

    def _sigint_handler(_signum: int, _frame: Any) -> None:
        nonlocal stop_requested
        stop_requested = True

    old_sigint = signal.getsignal(signal.SIGINT)
    signal.signal(signal.SIGINT, _sigint_handler)

    start_time = time.monotonic()
    capture_present_seen_at: Optional[float] = None

    pending_bmps: Dict[str, Path] = {}
    if source_bmp_path:
        pending_bmps["source"] = source_bmp_path
    if backbuffer_bmp_path:
        pending_bmps["backbuffer"] = backbuffer_bmp_path

    try:
        if once and not log_path.is_file():
            print(f"ERROR: Log file not found in --once mode: {log_path}", file=sys.stderr)
            return 2, state.build_report(str(log_path), capture_present)

        while not stop_requested:
            tailer.poll()

            # Check any pending BMP files for atomic completeness
            for label, bpath in list(pending_bmps.items()):
                try:
                    info = try_inspect_complete_bmp(bpath, label=label)
                except ValueError as exc:
                    print(f"ERROR: Malformed BMP '{bpath}': {exc}", file=sys.stderr)
                    del pending_bmps[label]
                    continue
                if info is not None:
                    state.record_bmp_inspection(info)
                    del pending_bmps[label]

            if once:
                tailer.flush_partial()
                break

            now = time.monotonic()
            if stop_after_present_num is not None and state.present_reached(capture_present):
                if capture_present_seen_at is None:
                    capture_present_seen_at = now
                if not pending_bmps or (now - capture_present_seen_at) >= bmp_wait_timeout_sec:
                    tailer.poll()
                    tailer.flush_partial()
                    break

            if max_wait_sec is not None and (now - start_time) >= max_wait_sec:
                tailer.flush_partial()
                break

            time.sleep(poll_interval_sec)
    finally:
        signal.signal(signal.SIGINT, old_sigint)

    report = state.build_report(str(log_path), capture_present)
    if json_path:
        try:
            if json_path.parent:
                json_path.parent.mkdir(parents=True, exist_ok=True)
            json_path.write_text(json.dumps(report, indent=2), encoding="utf-8")
        except OSError as exc:
            print(f"ERROR: Failed to write JSON report '{json_path}': {exc}", file=sys.stderr)
            return 2, report

    print(format_human_summary(report), flush=True)
    return 0, report


def main(argv: Optional[List[str]] = None) -> int:
    parser = argparse.ArgumentParser(
        prog="live_frame_monitor",
        description="Real-time log & BMP companion monitor for Hela D3D12-to-D3D11 proxy runs.",
    )
    parser.add_argument("log_path", help="Path to d3d12on11.log (or backup diagnostic log).")
    parser.add_argument(
        "--once",
        action="store_true",
        help="Read existing log file once from start to EOF and exit immediately (for fixtures/post-run logs).",
    )
    parser.add_argument(
        "--stop-after-present100",
        action="store_true",
        help="Stop automatically once Present #100 readbacks/history (and optional BMPs) are captured.",
    )
    parser.add_argument(
        "--stop-after-present",
        type=int,
        default=None,
        help="Stop after both source and backbuffer readbacks for this Present number complete.",
    )
    parser.add_argument(
        "--poll-interval",
        type=float,
        default=0.20,
        help="Polling interval in seconds when tailing live (default: 0.20).",
    )
    parser.add_argument(
        "--max-wait",
        type=float,
        default=None,
        help="Optional maximum monitoring duration in seconds before clean exit.",
    )
    parser.add_argument(
        "--source-bmp",
        dest="source_bmp",
        default=None,
        help="Optional path to source BMP to wait for and inspect when complete.",
    )
    parser.add_argument(
        "--backbuffer-bmp",
        dest="backbuffer_bmp",
        default=None,
        help="Optional path to backbuffer BMP to wait for and inspect when complete.",
    )
    parser.add_argument(
        "--bmp-wait-timeout",
        type=float,
        default=5.0,
        help="Seconds to wait for BMP completion after the target Present is logged (default: 5.0).",
    )
    parser.add_argument(
        "--json",
        dest="json_path",
        default=None,
        help="Optional output path for the structured JSON report.",
    )
    parser.add_argument(
        "--run-manifest",
        default=None,
        help="Optional hela-run-manifest/v1 to label the run and attest loaded module fingerprints.",
    )
    parser.add_argument(
        "--quiet-events",
        action="store_true",
        help="Suppress real-time [LIVE] lines and print only the final summary report.",
    )

    args = parser.parse_args(argv)
    rc, _ = run_monitor(
        log_path=Path(args.log_path),
        poll_interval_sec=args.poll_interval,
        once=args.once,
        stop_after_present100=args.stop_after_present100,
        stop_after_present_num=args.stop_after_present,
        max_wait_sec=args.max_wait,
        source_bmp_path=Path(args.source_bmp) if args.source_bmp else None,
        backbuffer_bmp_path=Path(args.backbuffer_bmp) if args.backbuffer_bmp else None,
        bmp_wait_timeout_sec=args.bmp_wait_timeout,
        json_path=Path(args.json_path) if args.json_path else None,
        quiet_events=args.quiet_events,
        run_manifest_path=Path(args.run_manifest) if args.run_manifest else None,
    )
    return rc


if __name__ == "__main__":
    sys.exit(main())
