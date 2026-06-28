#!/usr/bin/env python3
"""Validate graphite material calibration measurements against target bands."""

from __future__ import annotations

import argparse
import csv
from pathlib import Path


REQUIRED_BAND_COLUMNS = {
    "sample",
    "measurement",
    "target_min",
    "target_max",
    "observed",
}

ONE_SESSION_SAMPLES = (
    "MC01_4H_light_vellum",
    "MC02_3H_light_vellum",
    "MC03_2H_light_vellum",
    "MC04_H_light_vellum",
    "MC05_HB_medium_vellum",
    "MC06_B_medium_vellum",
    "MC07_2B_medium_vellum",
    "MC08_4B_heavy_vellum",
    "MC09_6B_heavy_vellum",
    "MC10_8B_heavy_vellum",
    "MC11_4H_heavy_vellum",
    "MC12_8B_light_vellum",
    "MC13_powder_smooth",
    "MC14_powder_vellum",
    "MC15_powder_rough",
    "MC16_powder_brush_redeposit_rough",
    "MC17_regular_eraser_lift_vellum",
    "MC18_regular_eraser_damage_vellum",
    "MC19_kneaded_lift_vellum",
    "MC20_tortillon_smudge_vellum",
    "MC21_fan_brush_soften_vellum",
    "MC22_8B_burnish_sheen_smooth",
)


def parse_float(value: str) -> float:
    try:
        return float(value)
    except ValueError as exc:
        raise ValueError(f"Invalid numeric value: {value}") from exc


def require_one_session(samples: list[str]) -> None:
    seen = set()
    duplicates = sorted({sample for sample in samples if sample in seen or seen.add(sample)})
    if duplicates:
        raise ValueError(f"Duplicate one-session samples: {', '.join(duplicates)}")

    actual = set(samples)
    required = set(ONE_SESSION_SAMPLES)
    missing = sorted(required.difference(actual))
    unexpected = sorted(actual.difference(required))
    if missing:
        raise ValueError(f"Missing one-session samples: {', '.join(missing)}")
    if unexpected:
        raise ValueError(f"Unexpected one-session samples: {', '.join(unexpected)}")


def analyze(path: Path, *, one_session_required: bool = False) -> tuple[int, int]:
    total = 0
    failures = 0
    samples: list[str] = []
    with path.open("r", newline="", encoding="utf-8") as handle:
        reader = csv.DictReader(handle)
        fieldnames = set(reader.fieldnames or [])
        band_mode = REQUIRED_BAND_COLUMNS.issubset(fieldnames)
        if not band_mode:
            missing = REQUIRED_BAND_COLUMNS.difference(fieldnames)
            raise ValueError(f"Missing required columns: {', '.join(sorted(missing))}")
        for row in reader:
            total += 1
            sample = row["sample"].strip()
            samples.append(sample)
            target_min = parse_float(row["target_min"])
            target_max = parse_float(row["target_max"])
            observed = parse_float(row["observed"])
            passed = target_min <= observed <= target_max
            if not passed:
                failures += 1
            status = "PASS" if passed else "FAIL"
            print(
                f"{status}  {sample} {row['measurement']}: "
                f"observed={observed:.4f} target={target_min:.4f}-{target_max:.4f}"
            )
    if one_session_required:
        require_one_session(samples)
    return total, failures


def main() -> int:
    parser = argparse.ArgumentParser(description="Analyze Graphite material calibration CSV files.")
    parser.add_argument("csv_path")
    parser.add_argument(
        "--require-one-session",
        action="store_true",
        help="Require the exact 22-swatch physical calibration session.",
    )
    args = parser.parse_args()
    path = Path(args.csv_path)
    if not path.exists():
        print(f"FAIL  calibration: missing file {path}")
        return 2
    try:
        total, failures = analyze(path, one_session_required=args.require_one_session)
    except ValueError as exc:
        print(f"FAIL  calibration: {exc}")
        return 2
    print(f"summary total={total} failures={failures}")
    return 0 if total > 0 and failures == 0 else 1


if __name__ == "__main__":
    raise SystemExit(main())
