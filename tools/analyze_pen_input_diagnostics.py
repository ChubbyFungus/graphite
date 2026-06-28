#!/usr/bin/env python3
"""Summarize Graphite native pen-input diagnostic captures."""

from __future__ import annotations

import argparse
import csv
from dataclasses import dataclass
from pathlib import Path


@dataclass
class Summary:
    packets: int = 0
    wm_pointer: int = 0
    wintab: int = 0
    realtime_stylus: int = 0
    mouse: int = 0
    min_pressure: float = 1.0
    max_pressure: float = 0.0
    raw_pressure_max: int = 0
    pressure_payload_packets: int = 0
    pen_info_packets: int = 0
    pointer_types: dict[int, int] | None = None
    pen_masks: dict[int, int] | None = None
    saw_tilt: bool = False
    saw_rotation: bool = False
    saw_eraser: bool = False
    saw_barrel: bool = False
    saw_tip: bool = False


def truthy(value: str | None) -> bool:
    return (value or "").strip() in {"1", "true", "True", "yes", "Yes"}


def parse_float(row: dict[str, str], key: str) -> float:
    try:
        return float(row.get(key, "0") or 0)
    except ValueError:
        return 0.0


def parse_int(row: dict[str, str], key: str) -> int:
    try:
        return int(float(row.get(key, "0") or 0))
    except ValueError:
        return 0


def analyze(path: Path) -> Summary:
    summary = Summary()
    summary.pointer_types = {}
    summary.pen_masks = {}
    with path.open("r", newline="", encoding="utf-8") as handle:
        reader = csv.DictReader(handle)
        for row in reader:
            summary.packets += 1
            source = row.get("source", "")
            if source == "WM_POINTER":
                summary.wm_pointer += 1
            elif source == "WinTab":
                summary.wintab += 1
            elif source == "RealTimeStylus":
                summary.realtime_stylus += 1
            elif source == "MouseFallback":
                summary.mouse += 1

            pressure = parse_float(row, "pressure")
            summary.min_pressure = min(summary.min_pressure, pressure)
            summary.max_pressure = max(summary.max_pressure, pressure)
            if truthy(row.get("hasPressure", "")):
                summary.pressure_payload_packets += 1
            if truthy(row.get("penInfoAvailable", "")):
                summary.pen_info_packets += 1
            pointer_type = parse_int(row, "pointerType")
            summary.pointer_types[pointer_type] = summary.pointer_types.get(pointer_type, 0) + 1
            pen_mask = parse_int(row, "penMask")
            summary.pen_masks[pen_mask] = summary.pen_masks.get(pen_mask, 0) + 1
            summary.raw_pressure_max = max(summary.raw_pressure_max, parse_int(row, "rawPressureMax"))
            summary.saw_tilt = summary.saw_tilt or parse_float(row, "tiltX") != 0.0 or parse_float(row, "tiltY") != 0.0
            summary.saw_rotation = summary.saw_rotation or truthy(row.get("hasRotation", ""))
            summary.saw_eraser = summary.saw_eraser or truthy(row.get("isEraser", ""))
            summary.saw_barrel = summary.saw_barrel or truthy(row.get("barrelButton", ""))
            summary.saw_tip = summary.saw_tip or truthy(row.get("isTip", ""))
    return summary


def status_line(label: str, passed: bool, detail: str) -> str:
    return f"{'PASS' if passed else 'CHECK'}  {label}: {detail}"


def format_counts(counts: dict[int, int] | None) -> str:
    if not counts:
        return "none"
    return " ".join(f"{key}:{value}" for key, value in sorted(counts.items()))


def main() -> int:
    parser = argparse.ArgumentParser(description="Analyze Graphite pen_input_diagnostics.csv captures.")
    parser.add_argument("csv_path", nargs="?", default="pen_input_diagnostics.csv")
    parser.add_argument("--require-pressure-variation", action="store_true")
    parser.add_argument("--require-tilt", action="store_true")
    parser.add_argument("--require-rotation", action="store_true")
    parser.add_argument("--require-eraser", action="store_true")
    parser.add_argument("--require-barrel", action="store_true")
    parser.add_argument("--require-wintab", action="store_true")
    parser.add_argument("--require-wm-pointer", action="store_true")
    parser.add_argument("--require-realtime-stylus", action="store_true")
    args = parser.parse_args()
    path = Path(args.csv_path)
    if not path.exists():
        print(f"CHECK  capture: missing file {path}")
        return 2

    summary = analyze(path)
    pressure_span = summary.max_pressure - summary.min_pressure if summary.packets else 0.0
    print(f"capture={path}")
    print(f"packets={summary.packets} realtime_stylus={summary.realtime_stylus} wm_pointer={summary.wm_pointer} wintab={summary.wintab} mouse={summary.mouse}")
    print(f"pressure_min={summary.min_pressure if summary.packets else 0.0:.3f} pressure_max={summary.max_pressure:.3f} raw_pressure_max={summary.raw_pressure_max} pressure_payload_packets={summary.pressure_payload_packets}")
    print(f"pen_info_packets={summary.pen_info_packets} pointer_type_counts={format_counts(summary.pointer_types)} pen_mask_counts={format_counts(summary.pen_masks)}")
    checks = {
        "packet stream": summary.packets > 0,
        "pointer/tablet source": summary.realtime_stylus > 0 or summary.wm_pointer > 0 or summary.wintab > 0,
        "pen payload": summary.realtime_stylus > 0 or summary.wintab > 0 or summary.pressure_payload_packets > 0 or summary.raw_pressure_max > 0 or summary.saw_tilt or summary.saw_rotation,
        "pressure variation": pressure_span > 0.05,
        "tip contact": summary.saw_tip,
        "tilt": summary.saw_tilt,
        "rotation": summary.saw_rotation,
        "eraser": summary.saw_eraser,
        "barrel": summary.saw_barrel,
        "wintab": summary.wintab > 0,
        "wm_pointer": summary.wm_pointer > 0,
        "realtime_stylus": summary.realtime_stylus > 0,
    }
    print(status_line("packet stream", checks["packet stream"], "input packets recorded"))
    print(status_line("pointer/tablet source", checks["pointer/tablet source"], "Windows Ink pointer/tablet packets present"))
    print(status_line("pen payload", checks["pen payload"], "pressure, tilt, or rotation pen fields present"))
    print(status_line("pressure variation", checks["pressure variation"], f"span {pressure_span:.3f}"))
    print(status_line("tip contact", checks["tip contact"], "tip-down packets observed"))
    print(status_line("tilt", checks["tilt"], "nonzero tiltX/tiltY observed"))
    print(status_line("rotation", checks["rotation"], "rotation-capable packets observed"))
    print(status_line("eraser", checks["eraser"], "eraser packets observed"))
    print(status_line("barrel", checks["barrel"], "barrel-button packets observed"))
    if summary.wm_pointer and summary.pointer_types and summary.pointer_types.get(4, 0) == summary.wm_pointer and summary.pen_masks and summary.pen_masks.get(0, 0) == summary.wm_pointer:
        print("CONCLUSION windows_ink: PT_PEN packets arrived, but Windows reported penMask=0 for every packet.")
    if args.require_wintab and summary.wintab == 0:
        print("CONCLUSION wintab: no WinTab packets were captured.")
    if summary.realtime_stylus == 0:
        print("CONCLUSION realtime_stylus: no Windows Ink RealTimeStylus packets were captured.")
    if summary.pressure_payload_packets == 0 and summary.raw_pressure_max == 0:
        print("CONCLUSION pressure: no hardware pressure payload was captured; displayed pressure is fallback only.")

    required = [
        checks["packet stream"],
        checks["pointer/tablet source"],
        checks["pen payload"],
        not args.require_pressure_variation or checks["pressure variation"],
        not args.require_tilt or checks["tilt"],
        not args.require_rotation or checks["rotation"],
        not args.require_eraser or checks["eraser"],
        not args.require_barrel or checks["barrel"],
        not args.require_wintab or checks["wintab"],
        not args.require_wm_pointer or checks["wm_pointer"],
        not args.require_realtime_stylus or checks["realtime_stylus"],
    ]
    return 0 if all(required) else 1


if __name__ == "__main__":
    raise SystemExit(main())
