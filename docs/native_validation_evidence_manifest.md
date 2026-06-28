# Native Validation Evidence Manifest

This file tracks external evidence for the CUDA-first native graphite app PRD.

The native app, tests, diagnostics, and analyzers exist. The entries below should be filled with real capture artifacts when the target hardware and reference materials are available.

## Verification Baseline

Run this before accepting any evidence:

```powershell
powershell -ExecutionPolicy Bypass -File tools\verify_native_slice.ps1
```

Run this before marking the PRD complete:

```powershell
powershell -ExecutionPolicy Bypass -File tools\verify_native_slice.ps1 -RequireCompleteEvidence
```

Expected current baseline:

- Native build passes.
- `Graphite.DocumentTests` passes.
- `Graphite.MaterialModelTests` passes.
- `Graphite.CudaMaterialSmokeTests` passes.
- `Graphite.PenDiagnosticsAnalyzer` passes.
- `Graphite.MaterialCalibrationAnalyzer` passes.
- `Graphite.MaterialCalibrationManifestCheck` passes.
- Native app smoke launch reports `started=True`.

## Pen-Display Evidence

Protocol: `docs/pen_display_validation_protocol.md`

Artifact path:

```text
pen_input_diagnostics.csv
```

Required verifier:

```powershell
powershell -ExecutionPolicy Bypass -File tools\verify_native_slice.ps1 -PenDiagnosticsCsv pen_input_diagnostics.csv -StrictPen
```

Evidence status:

```text
Device:
Driver:
Capture date:
Analyzer result:
Status: missing
Notes:
```

## Material Calibration Evidence

Protocol: `docs/graphite_material_calibration_protocol.md`

Artifact path:

```text
calibration/material_sessions/<session_id>/material_calibration_capture_manifest.csv
calibration/material_sessions/<session_id>/session_notes.md
calibration/material_sessions/<session_id>/physical/
calibration/material_sessions/<session_id>/measured/material_calibration_measured.csv
```

Required verifier:

```powershell
python tools\check_material_calibration_manifest.py calibration\material_sessions\<session_id>\material_calibration_capture_manifest.csv --require-ready --require-files
powershell -ExecutionPolicy Bypass -File tools\verify_native_slice.ps1 -MaterialCaptureManifestCsv calibration\material_sessions\<session_id>\material_calibration_capture_manifest.csv
python tools\analyze_material_calibration.py calibration\material_sessions\<session_id>\measured\material_calibration_measured.csv --require-one-session
powershell -ExecutionPolicy Bypass -File tools\verify_native_slice.ps1 -MaterialCalibrationCsv calibration\material_sessions\<session_id>\measured\material_calibration_measured.csv
```

Evidence status:

```text
Reference paper:
Reference pencils/tools:
Capture session:
Capture date:
Analyzer result:
Status: missing
Notes:
```

## Completion Rule

Do not mark the PRD complete until:

- The baseline verifier passes.
- A real pen-display diagnostic CSV passes the strict pen verifier, or any unsupported hardware capability is explicitly documented.
- A real material capture manifest passes the manifest verifier, and a generated measured calibration CSV passes the material calibration verifier, or out-of-band target choices are explicitly documented.
- Remaining limitations are reflected in `docs/cuda_d3d12_prd_completion_audit.md`.
