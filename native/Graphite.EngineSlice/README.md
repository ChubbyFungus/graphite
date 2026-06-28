# Graphite Native App

This is the native CUDA + D3D12 app path for the graphite engine goal.

The older WPF, browser, and Electron/WASM prototypes were extracted to `C:\Users\ftbal\graphite-non-cuda` on June 9, 2026. This native app proves the engine-facing path without moving drawing behavior into XAML or browser canvas code.

## What It Does

- Opens a native Win32 drawing window.
- Creates a D3D12 device, command queue, swap chain, and back-buffer presentation path.
- Uses `CudaGraphiteBackend` behind `IGraphiteBackend`.
- Keeps the document/event layer on `IGraphiteBackend` rather than the concrete CUDA backend.
- Runs CUDA kernels for:
  - paper tooth/height initialization
  - pencil deposition
  - regular eraser lift
  - kneaded eraser lift
  - tortillon/fan-brush graphite transport
  - powder-brush loose graphite deposit
  - display tone generation
  - derived surface sheen from paper height, graphite film, compaction, and damage
- Imports the D3D12 canvas texture into CUDA and writes display tone through a CUDA surface object.
- Keeps separate CUDA buffers for:
  - `paperHeight`
  - `paperRoughness`
  - `looseGraphite`
  - `boundGraphite`
  - `compaction`
  - `damage`
  - `paperBinding`
  - display pixels
- Provides paper presets for smooth bristol, cold press, and rough sketch paper; presets change the CUDA paper height, roughness/catch, and binding initialization.
- Converts `WM_POINTER` pen packets into normalized stroke packets through `InputAdapter`.
- Preserves pointer history packets when Windows exposes them.
- Preserves pen pressure when Windows exposes `PEN_MASK_PRESSURE`.
- Derives per-packet stroke orientation, velocity, and speed during input normalization.
- Preserves WM_POINTER pen rotation when Windows exposes it and feeds that rotation into the CUDA pencil footprint.
- Optionally loads `Wintab32.dll` and normalizes WinTab packets into the same stroke path when a tablet driver provides it, including pressure, twist/azimuth rotation, and altitude/azimuth-derived tilt axes.
- Records optional pen-input diagnostics to `pen_input_diagnostics.csv` from the real input path, including source API, raw pressure, normalized pressure, tilt, rotation, eraser/barrel state, position, velocity, and timestamps.
- Keeps mouse drawing as a fallback.
- Provides a native debug/control panel for tool choice and live backend/input status.
- Renders a Dear ImGui engine panel through the D3D12 frame for immediate-mode debug visibility and real tool settings.
- Lets the ImGui panel change active tool, pencil grade, tool radius, paper preset, and debug buffer view through the document/backend path.
- Records clear/stroke commands in `GraphiteDocument`; undo/redo rebuilds the canvas by replaying the document event log through the CUDA backend.
- Records per-stroke tile coverage metadata for the event log and reports replay coverage in the debug UI.
- Uses filtered dirty-tile replay for stroke undo/redo, while clear-event undo/redo still falls back to full replay.
- Includes `Graphite.DocumentTests` for document-layer dirty replay call-path coverage, including overlapping replay and non-overlap skip cases.
- Includes `Graphite.MaterialModelTests` for deterministic material rule coverage.
- Includes `Graphite.CudaMaterialSmokeTests` for CUDA/device-memory material smoke coverage.
- Includes `Graphite.MaterialCalibrationAnalyzer` for measured calibration CSV gate coverage and `Graphite.MaterialCalibrationManifestCheck` for physical capture manifest coverage.
- Provides CUDA-rendered buffer visualizers for display tone, loose graphite, bound graphite, paper height, compaction, damage, paper binding, paper roughness, and surface sheen.
- Keeps the canvas reference image as a separate visible guide layer and provides a separate sketch import action that converts decoded image tone into CUDA graphite material state.
- Tracks a 128px tile grid for active/touched canvas regions with CUDA-side active/touched tile masks mirrored back to the UI.
- Tracks CUDA-side material allocation state per tile; clear/new paper marks tiles unallocated and stroke touches lazily initialize only touched tile material.
- Stores material buffers through a tile page table into a growing CUDA page pool rather than one canvas-sized material slab; untouched paper stays procedural until a stroke allocates a physical material page.
- Uses the CUDA touched-tile mask for normal stroke-frame display updates, with full-frame CUDA rendering reserved for clear, restore, and debug-view changes.
- Compacts touched tiles into an index list so dirty-tile display launches scale with touched tiles instead of scanning the whole tile grid.
- Uses the same compact touched-tile list for stroke simulation launches, while each tile kernel still filters exact stroke distance per pixel.
- Reports whether the last display update used full-frame or dirty-tile rendering, plus render tile/pixel counts.
- Reports allocated material tile count separately from active/touched/render tiles.
- Reports physical material page usage versus current page-pool capacity.
- Tracks persistent CUDA-side graphite load for tortillon, fan brush, powder brush, and kneaded eraser so tools can pick up and redeposit material across strokes.
- Makes kneaded eraser lift weaker as its CUDA-side graphite load saturates, with slight redeposit from the carried load.
- Tracks paper damage as a CUDA material buffer affected by erasers and visible in rendering/debug views.
- Tracks paper binding/sizing as a CUDA material buffer that affects graphite catch, bound graphite, and eraser behavior.
- Computes a derived surface sheen response from paper-height gradients plus graphite and compaction state.

## Controls

- Pen or left mouse drag: HB pencil stroke.
- Pen eraser or right mouse drag: regular eraser.
- `C`: clear canvas.
- `1`: select hard `4H` behavior.
- `2`: select `HB` behavior.
- `3`: select soft `8B` behavior.
- `E`: regular eraser.
- `K`: kneaded eraser.
- `T`: tortillon.
- `F`: fan brush.
- `P`: powder brush.
- Paper picker: choose Smooth Bristol, Vellum Drawing, or Rough Sketch paper to start a fresh graphite surface.
- Canvas layers panel: load/clear a reference image overlay without changing the graphite material state.
- Canvas layers panel: import a decoded image as graphite material so erasers, tortillon, brushes, debug views, undo, and redo treat it like existing page graphite.
- Tool options panel: changes with the active tool; pencil options include sharpen/blunt tip state, and kneaded eraser options include blob, point, edge, and flat shapes.
- Tool options `Tab`: minimize the options panel to a side tab; click the tab to reopen it.
- `Ctrl+Z`: undo.
- `Ctrl+Y`: redo.
- `F10`: start/stop pen-input diagnostics CSV recording.
- `F1`: display tone view.
- `F2`: loose graphite view.
- `F3`: bound graphite view.
- `F4`: paper height view.
- `F5`: compaction view.
- `F6`: damage view.
- `F7`: paper binding view.
- `F8`: surface sheen view.
- `F9`: paper roughness view.

The window title and debug panel show the active tool, input source, pressure, stroke speed, input diagnostics status, CUDA kernel timing, packet count, history depth, replay tile coverage, tile/render mode, physical material page usage, and active buffer view. The ImGui panel also shows recorded event count and has a button for input CSV recording.

## Build

From `C:\Users\ftbal\graphite`:

```powershell
cmd /c "call ""C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat"" && cmake -S . -B build\engine-slice -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_CUDA_COMPILER=""C:\Program Files\NVIDIA GPU Computing Toolkit\CUDA\v13.1\bin\nvcc.exe"""
cmd /c "call ""C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat"" && cmake --build build\engine-slice --config Release"
```

Test:

```powershell
cmd /c "call ""C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat"" && ctest --test-dir build\engine-slice --output-on-failure"
```

Full native verification:

```powershell
powershell -ExecutionPolicy Bypass -File tools\verify_native_slice.ps1 -ExpectedCudaToolkitMajorMinor 13.1
```

Analyze a pen-input diagnostic capture:

```powershell
python tools\analyze_pen_input_diagnostics.py pen_input_diagnostics.csv
```

Strict hardware capability check:

```powershell
python tools\analyze_pen_input_diagnostics.py pen_input_diagnostics.csv --require-pressure-variation --require-tilt --require-rotation --require-eraser --require-barrel
```

Full verification with a strict pen capture:

```powershell
powershell -ExecutionPolicy Bypass -File tools\verify_native_slice.ps1 -PenDiagnosticsCsv pen_input_diagnostics.csv -StrictPen
```

Check a physical material capture manifest:

```powershell
python tools\check_material_calibration_manifest.py calibration\material_sessions\<session_id>\material_calibration_capture_manifest.csv --require-ready --require-files
powershell -ExecutionPolicy Bypass -File tools\verify_native_slice.ps1 -MaterialCaptureManifestCsv calibration\material_sessions\<session_id>\material_calibration_capture_manifest.csv
```

Full verification with measured material calibration evidence:

```powershell
python tools\analyze_material_calibration.py calibration\material_sessions\<session_id>\measured\material_calibration_measured.csv --require-one-session
powershell -ExecutionPolicy Bypass -File tools\verify_native_slice.ps1 -MaterialCalibrationCsv calibration\material_sessions\<session_id>\measured\material_calibration_measured.csv
```

Completion verifier:

```powershell
powershell -ExecutionPolicy Bypass -File tools\verify_native_slice.ps1 -RequireCompleteEvidence
```

Run:

```powershell
.\build\engine-slice\native\Graphite.EngineSlice\Graphite.NativeApp.exe
```

## Honest Limits

- CUDA is actually used for the material simulation.
- D3D12 is actually used for presentation.
- CUDA-D3D12 shared-resource interop is implemented for the display texture, and CUDA signals a D3D12 shared fence after display writes before D3D12 presents.
- Dear ImGui is integrated for an in-frame engine panel. The older native panel still exists as a simple fallback/control surface.
- WinTab is implemented as an optional dynamic adapter. It does not require `Wintab.h`, but it only becomes active when `Wintab32.dll` and a tablet context are available.
- Stroke velocity/orientation is derived from normalized packet movement. It is available to tools, and pencil/powder/transport behavior already responds to speed.
- WM_POINTER pen rotation is decoded when available. The dynamic WinTab adapter requests orientation/twist, maps it into the same rotation field, and derives tilt axes from altitude/azimuth, but that path still needs hardware verification with an actual WinTab tablet driver.
- Hardware verification now has a first-party capture path through `pen_input_diagnostics.csv`, but no real pen-display capture has been recorded in this environment.
- `tools/analyze_pen_input_diagnostics.py` summarizes a diagnostic CSV into packet-source, pressure, tilt, rotation, eraser, and barrel-button checks. Optional `--require-*` flags make hardware validation fail when expected capabilities are missing.
- `Graphite.PenDiagnosticsAnalyzer` keeps the diagnostic analyzer under CTest with a representative sample capture.
- `docs/pen_display_validation_protocol.md` gives the exact capture sequence, analyzer commands, and pass criteria for real tablet validation.
- `docs/graphite_material_calibration_protocol.md` gives the exact one-session physical capture workflow, manifest template, and later measured-calibration analyzer workflow.
- `docs/native_validation_evidence_manifest.md` tracks the remaining external evidence needed before calling the PRD complete.
- Undo/redo now replays clear/stroke events through the CUDA backend instead of restoring dense GPU snapshots.
- Imported sketches are raster material imports, not recovered source stroke histories. Common image files are decoded through WIC and converted into loose/bound graphite; `.kra`/Krita package parsing and true Krita brush-stroke recovery are not implemented in this native lane.
- Tile tracking now has CUDA-side active/touched/allocation masks plus lazy tile material initialization, page-table-backed material storage, compact tile-list stroke/update/display rendering, and clear/replay page-pool compaction. Fine-grained per-tile page release during local dirty edits is not implemented yet.
- Tool contamination is an early scalar load model, not a full per-fiber/per-bristle model.
- Damage is an early abrasion/fuzzing scalar, not a full paper-fiber fracture model.
- Binding/sizing is an early scalar material buffer, not full paper chemistry.
- Paper roughness/catch is now a separate scalar material buffer, not just an alias for paper height.
- Surface sheen is a derived CUDA render response, not a full anisotropic graphite BRDF.
- Stroke undo/redo uses filtered dirty-tile replay and has document-layer tests for the backend call path. Clear-event undo/redo still uses whole-document replay, and there is not yet a CUDA-backed material/image comparison test.
- Paper presets currently affect new/cleared paper state; they are not a full calibrated paper library.
- The native tool set is still early, but pencil, regular eraser, kneaded eraser, tortillon, fan brush, and powder brush now have material-state behavior.
- The graphite behavior is a first approximation, not a finished realism claim. CPU/CUDA material rule tests and a calibration analyzer exist, but full app-backend image/material comparison and real reference measurements do not yet.
