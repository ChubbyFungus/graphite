# Graphite CUDA App

This repo is the CUDA/D3D12 native Graphite app lane after the June 9, 2026 extraction.

## Source Of Truth

The active app is `Graphite.NativeApp` under `native/Graphite.EngineSlice`. It owns the Win32 shell, D3D12 presentation path, CUDA graphite/paper backend, pen input normalization, document/event replay, and native studio-tool UI.

The root browser app files (`index.html`, `app.js`, `style.css`, and `app.js.backup`) are intentionally removed from this repo. Do not run this lane through `npm`, Vite, Electron, Rust/WASM, WPF, or a browser canvas entry point.

The older non-CUDA app surfaces were extracted to the sibling folder `C:\Users\ftbal\graphite-non-cuda` on June 9, 2026:

- `legacy-browser`: original root browser canvas app and lab pages.
- `vite-wasm-electron`: Vite, Electron, and Rust/WASM prototype.
- `wpf-prototype`: WPF prototype formerly under `native/Graphite.Native`.

Those folders are reference material only. They are not build inputs for this repo.

## Build And Verify

Build and verification details for the CUDA app live in `native/Graphite.EngineSlice/README.md`.

The broad native verification gate is:

```powershell
powershell -ExecutionPolicy Bypass -File tools\verify_native_slice.ps1 -ExpectedCudaToolkitMajorMinor 13.1
```

The material calibration pass starts from `docs/graphite_material_calibration_protocol.md` and the fill-in CSV template at `docs/templates/material_calibration_one_session.csv`.

`tools/toolkit_layout_editor.html` is a local asset-layout helper for native tray imagery. It is not an app runtime surface and should not become the product lane.
