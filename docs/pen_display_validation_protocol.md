# Pen Display Validation Protocol

This protocol turns the native app input path into evidence for the CUDA/D3D12 graphite PRD.

## Goal

Prove that the target pen-display setup sends real drawing input into `Graphite.NativeApp`, not mouse-only fallback input.

The capture must show:

- WM_POINTER and/or WinTab packets from the pen display.
- Tip pressure with meaningful variation.
- Raw pressure range when available.
- Tilt or rotation when the hardware exposes it.
- Eraser and barrel-button packets if the pen supports them.
- Stable canvas coordinates and timestamps during drawing.

## Capture Steps

1. Build and launch the native app:

```powershell
powershell -ExecutionPolicy Bypass -File tools\verify_native_slice.ps1
.\build\engine-slice\native\Graphite.EngineSlice\Graphite.NativeApp.exe
```

2. Press `F10` or use the ImGui `Record input CSV` button.

3. Draw this sequence on the pen display:

- Light pencil stroke.
- Heavy pencil stroke.
- Slow tilted side stroke.
- Fast normal stroke.
- Barrel-button stroke or click, if the pen supports it.
- Eraser stroke, if the pen has an eraser end or eraser mode.

4. Press `F10` again or stop recording from ImGui.

5. Confirm that `pen_input_diagnostics.csv` exists in the repo root.

## Analysis Commands

Basic summary:

```powershell
python tools\analyze_pen_input_diagnostics.py pen_input_diagnostics.csv
```

Strict capability check for a full-feature pen:

```powershell
python tools\analyze_pen_input_diagnostics.py pen_input_diagnostics.csv --require-pressure-variation --require-tilt --require-rotation --require-eraser --require-barrel
```

Strict WM_POINTER check:

```powershell
python tools\analyze_pen_input_diagnostics.py pen_input_diagnostics.csv --require-wm-pointer --require-pressure-variation
```

Strict WinTab check:

```powershell
python tools\analyze_pen_input_diagnostics.py pen_input_diagnostics.csv --require-wintab --require-pressure-variation
```

Full verifier with strict pen analysis:

```powershell
powershell -ExecutionPolicy Bypass -File tools\verify_native_slice.ps1 -PenDiagnosticsCsv pen_input_diagnostics.csv -StrictPen
```

## Pass Criteria

Minimum pass:

- The analyzer reports `PASS packet stream`.
- The analyzer reports `PASS real pen source`.
- The analyzer reports `PASS pressure variation`.
- The CSV contains either `WM_POINTER` or `WinTab` rows.

Full-feature pass:

- Minimum pass criteria pass.
- Tilt is observed when the hardware supports tilt.
- Rotation is observed when the driver exposes rotation/twist.
- Eraser is observed when an eraser end or eraser mode exists.
- Barrel button is observed when the pen has one.

## Validation Notes Template

```text
Device:
Driver:
Connection:
Display mode:
WinTab available in UI:
Analyzer command:
Analyzer result:
Observed source APIs:
Pressure variation:
Tilt:
Rotation/twist:
Eraser:
Barrel:
Notes:
```

## Current Status

The app can capture and analyze this evidence, but no real pen-display capture has been recorded in this environment yet.
