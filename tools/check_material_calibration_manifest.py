#!/usr/bin/env python3
"""Check that a physical material-calibration capture manifest is complete."""

from __future__ import annotations

import argparse
import csv
from pathlib import Path


REQUIRED_COLUMNS = {
    "sample_id",
    "group",
    "paper",
    "tool",
    "physical_action",
    "photo_file",
    "status",
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


def check_manifest(path: Path, *, require_ready: bool, require_files: bool) -> tuple[int, int]:
    total = 0
    failures = 0
    samples: list[str] = []
    with path.open("r", newline="", encoding="utf-8") as handle:
        reader = csv.DictReader(handle)
        missing = REQUIRED_COLUMNS.difference(reader.fieldnames or [])
        if missing:
            raise ValueError(f"Missing required columns: {', '.join(sorted(missing))}")

        for row in reader:
            total += 1
            sample_id = row["sample_id"].strip()
            samples.append(sample_id)
            status = row["status"].strip().lower()
            photo_file = row["photo_file"].strip()
            row_failures: list[str] = []
            for column in ("sample_id", "group", "paper", "tool", "physical_action"):
                if not row[column].strip():
                    row_failures.append(f"missing {column}")
            if require_ready and status != "ready":
                row_failures.append(f"status is {status or 'blank'}")
            if require_ready and not photo_file:
                row_failures.append("missing photo_file")
            if require_files and photo_file:
                photo_path = path.parent / photo_file
                if not photo_path.exists():
                    row_failures.append(f"photo file not found: {photo_file}")

            if row_failures:
                failures += 1
                print(f"FAIL  {sample_id}: {'; '.join(row_failures)}")
            else:
                print(f"PASS  {sample_id}: {status or 'todo'}")

    require_one_session(samples)
    return total, failures


def main() -> int:
    parser = argparse.ArgumentParser(description="Check a Graphite material calibration capture manifest.")
    parser.add_argument("manifest_csv")
    parser.add_argument("--require-ready", action="store_true", help="Require every row to be marked ready.")
    parser.add_argument("--require-files", action="store_true", help="Require every photo_file path to exist.")
    args = parser.parse_args()

    path = Path(args.manifest_csv)
    if not path.exists():
        print(f"FAIL  material capture manifest missing: {path}")
        return 2

    try:
        total, failures = check_manifest(
            path,
            require_ready=args.require_ready,
            require_files=args.require_files,
        )
    except ValueError as exc:
        print(f"FAIL  material capture manifest: {exc}")
        return 2

    print(f"summary total={total} failures={failures}")
    return 0 if total > 0 and failures == 0 else 1


if __name__ == "__main__":
    raise SystemExit(main())
