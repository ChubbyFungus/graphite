# Native Architecture Report

This report is the current state of `C:\Users\ftbal\graphite` after the first CUDA-first native app pass.

For a requirement-by-requirement PRD checklist, see `docs/cuda_d3d12_prd_completion_audit.md`.

## Current Repo Architecture

This repo is now the CUDA/D3D12 app lane. The older non-CUDA app surfaces were extracted to the sibling folder `C:\Users\ftbal\graphite-non-cuda` on June 9, 2026:

- Legacy browser app: `C:\Users\ftbal\graphite-non-cuda\legacy-browser`.
- Vite/Rust/WASM/Electron prototype: `C:\Users\ftbal\graphite-non-cuda\vite-wasm-electron`.
- Native WPF prototype: `C:\Users\ftbal\graphite-non-cuda\wpf-prototype\Graphite.Native`.

Those extracted surfaces remain useful references for product behavior and early graphite math, but they are not part of the CUDA app build path in this repo.

## Native App Path Added

The target-direction native path now starts at:

- `native/Graphite.EngineSlice/src/main.cpp`
- `native/Graphite.EngineSlice/src/engine_document.*`
- `native/Graphite.EngineSlice/src/debug_panel.*`
- `native/Graphite.EngineSlice/src/input_adapter.*`
- `native/Graphite.EngineSlice/src/igraphite_backend.h`
- `native/Graphite.EngineSlice/src/cuda_graphite_backend.*`
- `native/Graphite.EngineSlice/src/d3d12_renderer.*`

The executable target is `Graphite.NativeApp`.

## Data Flow

The current native app flow is:

```text
Win32 window/messages
  -> InputAdapter
  -> normalized StrokePacket stream
  -> GraphiteDocument stroke/event lifecycle
  -> IGraphiteBackend
  -> CudaGraphiteBackend CUDA material buffers
  -> CUDA displayTone surface writes into imported D3D12 texture
  -> D3D12 swap-chain present
```

This is intentionally not a generic brush app. The CUDA backend keeps separate material state for paper height, paper roughness/catch, paper binding, loose graphite, bound graphite, compaction, and damage, then derives display tone and surface sheen from that state.

## What Is Real Now

- Native Win32 shell exists.
- D3D12 device and swap-chain presentation exist.
- CUDA kernels update graphite material state.
- D3D12 creates a shared canvas texture and CUDA imports it as external memory.
- CUDA writes display tone directly into the imported D3D12 texture through a surface object.
- `IGraphiteBackend` keeps CUDA behind an interface.
- `InputAdapter` normalizes `WM_POINTER` and fallback mouse input.
- `WinTabAdapter` dynamically loads `Wintab32.dll` and normalizes WinTab packets when a driver is available, including pressure, twist/azimuth rotation, and altitude/azimuth-derived tilt axes.
- Pen pressure, raw pressure, raw screen position, tilt fields, WM_POINTER rotation when available, derived orientation, derived velocity/speed, eraser state, barrel state, and packet source are represented in `StrokePacket`.
- `PenInputDiagnostics` can record the real input stream to `pen_input_diagnostics.csv` for hardware validation of WM_POINTER and WinTab packet behavior.
- `tools/analyze_pen_input_diagnostics.py` summarizes diagnostic captures so hardware validation has a repeatable result instead of a feel-only check.
- `Graphite.PenDiagnosticsAnalyzer` runs the diagnostic analyzer against a representative sample capture in CTest.
- `docs/pen_display_validation_protocol.md` defines the real pen-display capture sequence, strict analyzer commands, pass criteria, and notes template.
- `docs/native_validation_evidence_manifest.md` tracks external pen-display and material-calibration artifacts that still need real-world evidence.
- Pointer history packets are preserved when `GetPointerPenInfoHistory` provides them.
- Input normalization derives stroke speed/orientation before packets reach the document and CUDA backend.
- Input diagnostics expose source API packet counts, pressure range, tilt/rotation presence, eraser, and barrel-button evidence in the UI while recording a CSV.
- Native CUDA tool behavior exists for pencil, regular eraser, kneaded eraser, tortillon, fan brush, and powder brush.
- Paper presets exist for smooth bristol, cold press, and rough sketch paper; they change CUDA paper height/tooth, roughness/catch, and binding initialization when creating a new surface.
- `GraphiteDocument` owns tool state, stroke lifecycle, clear/stroke event recording, per-stroke tile coverage metadata, and history depth instead of leaving those responsibilities in the Win32 message loop.
- `GraphiteDocument` now depends on `IGraphiteBackend`, not the concrete CUDA backend, so CUDA remains behind the backend seam.
- `DebugPanel` provides visible native controls and live backend/input state without owning drawing behavior.
- Dear ImGui is integrated into the D3D12 present path for an immediate-mode engine panel with real controls for active tool, pencil grade, radius, paper preset, and debug buffer view.
- Undo/redo replays recorded clear/stroke document events through the CUDA backend rather than restoring screen pixels or dense GPU snapshots.
- Stroke events record touched tile coverage and expose replay coverage telemetry. Stroke undo/redo uses that coverage to clear and replay only affected tiles through a backend tile filter.
- `Graphite.DocumentTests` verifies that stroke undo/redo uses dirty tile backend calls instead of silently falling back to full document replay, including a non-overlap case that must not replay unrelated strokes.
- `Graphite.MaterialModelTests` guards key material rules in a deterministic CPU model: softer pencil deposits more than hard pencil, regular and kneaded erasers differ, and powder responds to paper tooth.
- `Graphite.CudaMaterialSmokeTests` exercises CUDA kernels and device memory for core material directions: pencil deposition, eraser removal/damage, and powder tooth response.
- `docs/graphite_material_calibration_protocol.md`, `docs/templates/material_calibration_one_session.csv`, `tools/check_material_calibration_manifest.py`, and `tools/analyze_material_calibration.py` define the physical capture manifest and later measured calibration gate; `Graphite.MaterialCalibrationManifestCheck` and `Graphite.MaterialCalibrationAnalyzer` keep both stages covered by CTest.
- CUDA buffer visualizer views exist for display tone, loose graphite, bound graphite, paper height, compaction, damage, paper binding, paper roughness, and surface sheen.
- The native backend tracks a 128px tile grid with CUDA-side active/touched tile masks, mirrors counts back to the UI, and uses touched tiles for normal stroke-frame display rendering.
- Dirty-tile display rendering now uses a compact touched-tile index list, so the render launch scales with touched tile count rather than scanning the whole tile grid.
- Stroke simulation also launches over the compact touched-tile index list, then filters exact stroke coverage inside each tile.
- Material state is lazily initialized per touched tile after clear/new paper using a CUDA-side allocated-tile mask; untouched tiles render procedurally from the active paper preset.
- Material buffers are indexed through a host-managed tile page table into a growing CUDA page pool. Untouched paper tiles keep using procedural defaults and do not receive physical material pages until a stroke touches them.
- Backend stats expose whether the last display update was full-frame or dirty-tile rendering, with render tile/pixel counts shown in the UI.
- Backend stats expose allocated material tile count separately from active/touched/render tiles.
- Backend stats expose physical material page usage and current page-pool capacity in the ImGui and native debug panels.
- Tortillon, fan brush, powder brush, and kneaded eraser now have persistent CUDA-side graphite load values for pickup/redeposit behavior.
- Kneaded eraser lifting responds to saturation, so it becomes less effective as its carried graphite load rises and can faintly redeposit.
- Paper damage is represented as a CUDA material buffer and contributes to eraser abrasion, powder catch, and final tone.
- Paper binding/sizing is represented as a CUDA material buffer and contributes to graphite catch, bound graphite, and eraser behavior.
- Paper roughness/catch is represented as a CUDA material buffer and contributes to pencil catch and powder catch separately from geometric height.
- Surface sheen is computed in CUDA from paper-height gradients, graphite film, compaction, and damage, and contributes to the display-tone render path.
- Pencil deposition, powder deposit, and tortillon/fan transport now respond to normalized stroke speed.
- Pencil stroke footprint responds to WM_POINTER pen rotation when available and otherwise falls back to derived stroke direction.

## What Is Not Done Yet

- WinTab support is implemented as an optional dynamic adapter, but it is runtime-dependent on `Wintab32.dll` and an available tablet context and has not been hardware-verified here.
- WM_POINTER rotation is decoded when Windows provides it. The dynamic WinTab adapter requests orientation/twist, maps twist/azimuth into `StrokePacket::rotation`, and maps altitude/azimuth into tilt axes, but that path still needs hardware verification with an actual WinTab tablet driver.
- No hardware diagnostic CSV has been captured here yet; the app now has the capture path needed to produce one on the target pen display.
- Sparse material allocation is implemented as a host-managed page table plus growing CUDA material page pool. Clear and document replay compact the physical page pool; fine-grained per-tile page release during local dirty edits is not implemented yet.
- Tool contamination is implemented as scalar per-tool load, not a full detailed tool-material model.
- Paper damage is implemented as a scalar abrasion/fuzzing buffer, not a detailed fiber model.
- Paper binding/sizing is implemented as a scalar buffer, not a full paper chemistry model.
- Paper roughness/catch is implemented as a scalar buffer, not a full fiber network.
- Surface sheen is a derived CUDA lighting/material response, not a full anisotropic graphite BRDF or stored normal map.
- Paper presets are first-pass material presets, not a calibrated paper catalog.
- Tool behavior is still approximate; tortillon/fan/powder are material-transport/deposit approximations, not finished realism.
- Stroke undo/redo uses filtered dirty-tile replay. Clear-event undo/redo still uses whole-document replay.
- The graphite model is a first approximation, not a finished realism claim. CPU/CUDA material rule tests and a calibration gate exist, but full app-backend image/material comparison and real reference measurements are still needed.

## Build And Run

Configure:

```powershell
cmd /c "call ""C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat"" && cmake -S . -B build\engine-slice -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_CUDA_COMPILER=""C:\Program Files\NVIDIA GPU Computing Toolkit\CUDA\v13.1\bin\nvcc.exe"""
```

Build:

```powershell
cmd /c "call ""C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat"" && cmake --build build\engine-slice --config Release"
```

Test:

```powershell
cmd /c "call ""C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat"" && ctest --test-dir build\engine-slice --output-on-failure"
```

Full native verification:

```powershell
powershell -ExecutionPolicy Bypass -File tools\verify_native_slice.ps1
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

Analyze pen-input capture:

```powershell
python tools\analyze_pen_input_diagnostics.py pen_input_diagnostics.csv
```

Strict pen capability check:

```powershell
python tools\analyze_pen_input_diagnostics.py pen_input_diagnostics.csv --require-pressure-variation --require-tilt --require-rotation --require-eraser --require-barrel
```

Run:

```powershell
.\build\engine-slice\native\Graphite.EngineSlice\Graphite.NativeApp.exe
```
