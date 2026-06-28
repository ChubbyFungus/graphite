# CUDA/D3D12 Graphite PRD Completion Audit

This audit maps `docs/cuda_d3d12_graphite_engine_goal.md` to the current native implementation under `native/Graphite.EngineSlice`.

## Objective

Build a native Windows graphite drawing app that uses CUDA as the primary graphite/paper simulation backend, D3D12 for presentation, real pen-display input paths, Dear ImGui for engine UI, and a material-state graphite model rather than a generic opacity brush.

## Current Architecture Evidence

- Native target: `Graphite.NativeApp` in `native/Graphite.EngineSlice/CMakeLists.txt`.
- App shell: `native/Graphite.EngineSlice/src/main.cpp`.
- Input adapters: `input_adapter.*` for `WM_POINTER`, `wintab_adapter.*` for dynamic WinTab.
- Hardware validation protocol: `docs/pen_display_validation_protocol.md`.
- Evidence manifest: `docs/native_validation_evidence_manifest.md`.
- Engine/document layer: `engine_document.*`.
- Backend interface: `igraphite_backend.h`.
- CUDA backend: `cuda_graphite_backend.*`.
- D3D12 renderer and CUDA interop: `d3d12_renderer.*`.
- UI: Dear ImGui rendered through the D3D12 present path, plus a native fallback panel.

## Requirement Checklist

| PRD requirement | Current evidence | Status |
| --- | --- | --- |
| C++20 native Windows app | `Graphite.NativeApp` Win32 executable target | Implemented |
| CUDA-first graphite/paper simulation | `CudaGraphiteBackend` CUDA kernels update material buffers | Implemented |
| D3D12 display/presentation | `D3D12Renderer` creates device, swap chain, command queue, render targets | Implemented |
| CUDA-D3D12 interop | D3D12 shared texture imported into CUDA; CUDA writes display surface; shared fence used | Implemented |
| Win32 shell first | `main.cpp` owns Win32 window/message loop | Implemented |
| Dear ImGui tool/debug UI | ImGui panel controls tool, grade, radius, paper preset, debug view | Implemented |
| WM_POINTER baseline pen input | `InputAdapter::packetsFromPointer` normalizes pointer/pen history | Implemented |
| WinTab path | `WinTabAdapter` dynamically loads `Wintab32.dll` and normalizes packets | Implemented, needs hardware verification |
| No WPF-first rewrite | Native path is separate from the extracted WPF prototype at `C:\Users\ftbal\graphite-non-cuda\wpf-prototype\Graphite.Native` | Satisfied |
| No HLSL fallback | No HLSL fallback path added | Satisfied |
| No NPU/DirectML | No NPU/DirectML features added | Satisfied |
| UI/input/engine/CUDA/render separation | Main/app, input adapters, `GraphiteDocument`, `IGraphiteBackend`, CUDA backend, D3D12 renderer are separate | Implemented |
| CUDA behind backend interface | `GraphiteDocument` depends on `IGraphiteBackend`, not concrete CUDA backend | Implemented |
| Input normalized before tools | `StrokePacket` produced before backend tool kernels consume it | Implemented |
| Canvas x/y and raw screen x/y | `StrokePacket` fields populated by input adapters | Implemented |
| Pressure | WM_POINTER and WinTab pressure normalized with raw pressure fields | Implemented |
| Tilt | WM_POINTER tilt fields preserved when available; WinTab orientation altitude/azimuth is mapped into normalized tilt axes | Implemented, hardware verification needed |
| Rotation/orientation | WM_POINTER rotation and dynamic WinTab orientation/twist mapped; derived stroke orientation exists | Implemented, WinTab hardware verification needed |
| Timestamp | `StrokePacket::timestampUs` set | Implemented |
| Velocity | Derived velocity/speed added during input normalization | Implemented |
| Tip/eraser/barrel/source/raw info | `StrokePacket` contains these fields; WM_POINTER/WinTab adapters populate what APIs expose | Implemented |
| Hardware input diagnostics | `PenInputDiagnostics` records real input packets to `pen_input_diagnostics.csv`; `tools/analyze_pen_input_diagnostics.py` summarizes source, pressure variation, tilt, rotation, eraser, barrel, position, velocity, timestamps and supports strict `--require-*` capability gates; CTest covers analyzer behavior with a representative capture | Implemented, needs real hardware capture |
| Hardware validation protocol | `docs/pen_display_validation_protocol.md` defines capture steps, strict analysis commands, pass criteria, and validation notes template | Implemented, needs real hardware execution |
| Evidence manifest | `docs/native_validation_evidence_manifest.md` tracks required baseline verifier, pen-display capture, material calibration evidence, and completion rule | Implemented, evidence entries missing |
| Paper height/tooth | `paperHeight` CUDA buffer | Implemented |
| Paper roughness/catch | `paperRoughness` CUDA buffer | Implemented |
| Paper sizing/binding | `paperBinding` CUDA buffer | Implemented |
| Bound graphite | `boundGraphite` CUDA buffer | Implemented |
| Loose graphite | `looseGraphite` CUDA buffer | Implemented |
| Compaction/burnish | `compaction` CUDA buffer influences tone and sheen | Implemented |
| Paper damage/fuzz | `damage` CUDA buffer | Implemented |
| Tool contamination/load | CUDA tool load channels for tortillon, fan brush, powder brush, kneaded eraser | Implemented as scalar model |
| Display tone | CUDA render kernels write display tone into D3D12-shared surface | Implemented |
| Surface normals/sheen | Derived sheen from paper-height gradients, graphite film, compaction, damage | Implemented as derived approximation |
| Graphite pencil | CUDA pencil deposition with grade, pressure, speed, tilt/rotation footprint | Implemented |
| Regular eraser | CUDA material removal, compaction, damage, binding reduction | Implemented |
| Kneaded eraser | Selective lifting with saturation/load and slight redeposit | Implemented |
| Tortillon | Directional loose graphite transport/redeposit and compaction | Implemented |
| Fan brush | Softer loose graphite redistribution | Implemented |
| Powder brush | Powder/loose graphite deposit affected by roughness/tooth | Implemented |
| Material behavior regression tests | `Graphite.MaterialModelTests` anchors CPU material rules; `Graphite.CudaMaterialSmokeTests` runs CUDA kernels/device memory checks for pencil deposition, eraser removal/damage, and powder tooth response | Implemented, full app-backend parity/reference calibration still needed |
| Material calibration protocol | `docs/graphite_material_calibration_protocol.md`, `docs/templates/material_calibration_one_session.csv`, `tools/check_material_calibration_manifest.py`, and `tools/analyze_material_calibration.py` define the physical capture manifest and later measured calibration gate; `Graphite.MaterialCalibrationManifestCheck` covers the manifest and `Graphite.MaterialCalibrationAnalyzer` covers measured CSVs | Implemented, needs real reference captures |
| Undo/redo/event log | `GraphiteDocument` records clear/stroke events, records per-stroke tile coverage metadata, and uses filtered tile replay for stroke undo/redo; document tests verify overlapping replay and non-overlap skip behavior; clear events still use full replay | Implemented, clear-boundary replay still full |
| Tiled paper state | CUDA-side active/touched/allocation tile masks, allocated tile telemetry, lazy touched-tile material initialization, compact tile-list stroke updates, dirty-tile display rendering | Implemented, still early |
| Sparse GPU material allocation | Host-managed tile page table maps touched tiles into a growing CUDA material page pool; untouched tiles render procedurally without physical material pages; clear/replay compacts the physical page pool | Implemented, still early |
| Paper presets | Smooth bristol, cold press, rough sketch presets alter CUDA paper initialization | Implemented, first-pass presets |
| Performance/input monitors | ImGui/native panel expose packet count, kernel time, tile/render mode, material page pool usage, input source, diagnostics status, WinTab status | Implemented |

## Verification Commands

Configure:

```powershell
cmd /c "call ""C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat"" && cmake -S . -B build\engine-slice -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_CUDA_COMPILER=""C:\Program Files\NVIDIA GPU Computing Toolkit\CUDA\v13.1\bin\nvcc.exe"""
```

Build:

```powershell
cmd /c "call ""C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat"" && cmake --build build\engine-slice --config Release"
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

Completion verifier:

```powershell
powershell -ExecutionPolicy Bypass -File tools\verify_native_slice.ps1 -RequireCompleteEvidence
```

Document replay tests:

```powershell
cmd /c "call ""C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat"" && ctest --test-dir build\engine-slice --output-on-failure"
```

Analyze pen-input diagnostics:

```powershell
python tools\analyze_pen_input_diagnostics.py pen_input_diagnostics.csv
```

Strict pen capability check:

```powershell
python tools\analyze_pen_input_diagnostics.py pen_input_diagnostics.csv --require-pressure-variation --require-tilt --require-rotation --require-eraser --require-barrel
```

Smoke launch:

```powershell
$p = Start-Process -FilePath .\build\engine-slice\native\Graphite.EngineSlice\Graphite.NativeApp.exe -PassThru; Start-Sleep -Seconds 3; $alive = -not $p.HasExited; if ($alive) { $p.CloseMainWindow() | Out-Null; Start-Sleep -Milliseconds 500; if (-not $p.HasExited) { Stop-Process -Id $p.Id -Force } }; "started=$alive exitCode=$($p.ExitCode)"
```

## Remaining Gaps

- Sparse material page lifetime: active/touched/allocation tile masks, allocated-tile telemetry, lazy tile material initialization, a tile page table, a growing CUDA material page pool, tile-list stroke updates, dirty-tile rendering, and clear/replay compaction exist. Fine-grained per-tile page release during local edits is not implemented yet.
- Hardware tablet verification: WM_POINTER builds and normalizes input, WinTab is dynamically wired, packet diagnostics can be captured to CSV, captures can be summarized with `tools/analyze_pen_input_diagnostics.py`, and the analyzer is under CTest. No real Wacom/pro tablet capture has been run here.
- WinTab tilt hardware validation: dynamic WinTab maps orientation altitude/azimuth into tilt axes, but this still needs testing against a real driver/tablet because WinTab packet orientation conventions vary by device.
- Replay performance: stroke undo/redo uses recorded tile coverage to clear and replay only affected tiles, and `Graphite.DocumentTests` covers overlapping replay plus non-overlap skip behavior. Clear-event undo/redo still uses full replay.
- Physical fidelity: material behavior is meaningful and stateful, with CPU/CUDA material regression tests and a calibration analyzer/protocol, but real reference swatches have not been measured yet.

## Next Engineering Steps

1. Add fine-grained tile-page release for local dirty edits instead of only clear/replay compaction.
2. Run `F10` input diagnostics on a real pen display and commit/record the validation notes for WM_POINTER and WinTab packet behavior.
3. Add full app-backend CUDA image/material comparison tests for overlapping stroke undo/redo.
4. Capture real graphite/paper reference swatches, validate the capture manifest, then generate measured calibration targets for tuning material constants.
