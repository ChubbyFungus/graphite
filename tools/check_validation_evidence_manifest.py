#!/usr/bin/env python3
"""Check whether the native validation evidence manifest still has missing evidence."""

from __future__ import annotations

import argparse
from pathlib import Path


def main() -> int:
    parser = argparse.ArgumentParser(description="Check Graphite native validation evidence manifest status.")
    parser.add_argument("manifest", nargs="?", default="docs/native_validation_evidence_manifest.md")
    parser.add_argument("--allow-missing", action="store_true", help="Report missing entries but exit successfully.")
    args = parser.parse_args()

    path = Path(args.manifest)
    if not path.exists():
        print(f"FAIL  evidence manifest missing: {path}")
        return 2

    text = path.read_text(encoding="utf-8")
    missing_count = text.count("Status: missing")
    if missing_count:
        print(f"CHECK evidence manifest: {missing_count} missing evidence entr{'y' if missing_count == 1 else 'ies'}")
        if "pen_input_diagnostics.csv" in text:
            print("CHECK pen evidence: run docs/pen_display_validation_protocol.md on target hardware")
        if "material_calibration_capture_manifest.csv" in text or "material_calibration_measured.csv" in text:
            print("CHECK material evidence: run docs/graphite_material_calibration_protocol.md with real references")
        return 0 if args.allow_missing else 1

    print("PASS  evidence manifest: no missing evidence entries")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
