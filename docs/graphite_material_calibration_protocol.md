# Graphite Material Calibration Protocol

This is the one-session physical capture workflow for the native Graphite material model.

The important distinction:

- The physical session produces labeled real-world evidence.
- Codex ingests that evidence later and turns it into numeric calibration targets.
- The native app is compared against those numeric targets after the physical evidence exists.

## Source Of Truth

The physical swatches are the target. The first-pass CSV is only a manifest that says what each swatch is and where its photo or scan lives.

Do not fill in `physical_value`, `observed`, tolerance bands, or other numeric calibration values during the physical pass. Those are generated later from the captured images.

## Hard Reject If

- A sample ID from `docs/templates/material_calibration_one_session.csv` is renamed.
- A row is marked `ready` before its `photo_file` is filled.
- A missing physical tool is silently replaced with a different tool.
- Physical photos in the same paper group use different lighting, exposure, or white balance.
- The physical swatch is not fresh blank paper unless the row explicitly says to reuse a prior swatch.
- The filled manifest is not checked before Codex ingests it.

## Success Shape

One physical session should leave this artifact set:

```text
calibration/material_sessions/<session_id>/
  material_calibration_capture_manifest.csv
  session_notes.md
  physical/
    MC01_4H_light_vellum.png
    ...
```

`material_calibration_capture_manifest.csv` should be a filled copy of `docs/templates/material_calibration_one_session.csv`.

`session_notes.md` should be a filled copy of `docs/templates/material_calibration_session_notes.md`.

Codex may later create derived artifacts in the same folder, for example:

```text
  measured/
    material_calibration_measured.csv
```

That later measured CSV is the one used by `tools/analyze_material_calibration.py`.

## Setup

Use these physical materials if available:

- Smooth bristol paper.
- Vellum or medium-tooth drawing paper.
- Rough sketch paper.
- 4H, 3H, 2H, H, HB, B, 2B, 4B, 6B, and 8B graphite pencils.
- Vinyl or standard regular eraser.
- Kneaded eraser.
- Tortillon.
- Fan brush.
- Powder brush.
- Loose graphite powder.

Use fixed lighting for the whole session. Lock camera exposure and white balance if possible. Put a blank patch of the same paper near each swatch or keep an untouched baseline region on every sheet.

## Create The Session Files

From `C:\Users\ftbal\graphite`:

```powershell
New-Item -ItemType Directory -Force calibration\material_sessions\<session_id>\physical
Copy-Item docs\templates\material_calibration_one_session.csv calibration\material_sessions\<session_id>\material_calibration_capture_manifest.csv
Copy-Item docs\templates\material_calibration_session_notes.md calibration\material_sessions\<session_id>\session_notes.md
```

Use a concrete session ID, for example `2026-06-14-first-pass`.

## Exact Swatches

Every swatch uses a fresh blank area unless the row says to reuse another sample. Keep pencil strokes about 60 mm long. For light pressure, use barely-more-than-contact pressure. For medium pressure, use normal writing pressure. For heavy pressure, use firm pressure without tearing the paper.

| ID | What To Make |
| --- | --- |
| `MC01_4H_light_vellum` | Vellum paper, 4H pencil, 5 light strokes |
| `MC02_3H_light_vellum` | Vellum paper, 3H pencil, 5 light strokes |
| `MC03_2H_light_vellum` | Vellum paper, 2H pencil, 5 light strokes |
| `MC04_H_light_vellum` | Vellum paper, H pencil, 5 light strokes |
| `MC05_HB_medium_vellum` | Vellum paper, HB pencil, 5 medium strokes |
| `MC06_B_medium_vellum` | Vellum paper, B pencil, 5 medium strokes |
| `MC07_2B_medium_vellum` | Vellum paper, 2B pencil, 5 medium strokes |
| `MC08_4B_heavy_vellum` | Vellum paper, 4B pencil, 5 heavy strokes |
| `MC09_6B_heavy_vellum` | Vellum paper, 6B pencil, 5 heavy strokes |
| `MC10_8B_heavy_vellum` | Vellum paper, 8B pencil, 5 heavy strokes |
| `MC11_4H_heavy_vellum` | Vellum paper, 4H pencil, 5 heavy strokes |
| `MC12_8B_light_vellum` | Vellum paper, 8B pencil, 5 light strokes |
| `MC13_powder_smooth` | Smooth bristol, one 25 mm graphite powder patch, then one soft powder-brush pass |
| `MC14_powder_vellum` | Vellum paper, one 25 mm graphite powder patch, then one soft powder-brush pass |
| `MC15_powder_rough` | Rough sketch paper, one 25 mm graphite powder patch, then one soft powder-brush pass |
| `MC16_powder_brush_redeposit_rough` | Load powder brush from `MC15`, then drag 60 mm into blank rough paper |
| `MC17_regular_eraser_lift_vellum` | Fresh 8B heavy block on vellum, erase half with 3 firm vinyl-eraser passes |
| `MC18_regular_eraser_damage_vellum` | Use the erased half from `MC17`, then photograph the surface with raking light |
| `MC19_kneaded_lift_vellum` | Fresh 8B medium block on vellum, press-lift 5 times with kneaded point |
| `MC20_tortillon_smudge_vellum` | Fresh 8B heavy source block on vellum, drag tortillon 60 mm into blank paper |
| `MC21_fan_brush_soften_vellum` | Fresh hard-edged 8B patch on vellum, brush across it with 5 light fan-brush passes |
| `MC22_8B_burnish_sheen_smooth` | Smooth bristol, 20 tight heavy 8B strokes over a 25 mm square, oblique photo |

## Fill The Manifest

The physical-session CSV uses this simple manifest schema:

```csv
sample_id,group,paper,tool,physical_action,photo_file,status,notes
```

During the session, only fill these fields:

- `photo_file`: the image path for that swatch, usually `physical/<sample_id>.png`.
- `status`: change from `todo` to `ready` only when the photo exists.
- `notes`: anything about tool substitution, lighting, paper mismatch, or a weak/retaken swatch.

Everything else is prefilled so Codex knows what the swatch was supposed to be.

## Check The Physical Capture

After the photos/scans exist, run:

```powershell
python tools\check_material_calibration_manifest.py calibration\material_sessions\<session_id>\material_calibration_capture_manifest.csv --require-ready --require-files
```

Or run it through the broader native verifier:

```powershell
powershell -ExecutionPolicy Bypass -File tools\verify_native_slice.ps1 -MaterialCaptureManifestCsv calibration\material_sessions\<session_id>\material_calibration_capture_manifest.csv
```

This proves the physical evidence is complete enough for Codex to ingest. It does not claim the native app is calibrated yet.

## Later Codex Ingest

After the manifest passes, Codex can read the photos, measure each swatch against its blank-paper baseline, and generate a measured calibration CSV.

That later generated CSV is checked with:

```powershell
python tools\analyze_material_calibration.py calibration\material_sessions\<session_id>\measured\material_calibration_measured.csv --require-one-session
powershell -ExecutionPolicy Bypass -File tools\verify_native_slice.ps1 -MaterialCalibrationCsv calibration\material_sessions\<session_id>\measured\material_calibration_measured.csv
```

## Current Status

The physical capture manifest, manifest checker, measured-output analyzer, and native verifier hooks exist. Real reference swatches have not been collected yet.
