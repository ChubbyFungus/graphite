# Graphite CUDA App

A native Windows sketching app that recreates the feel of graphite pencil on paper by modeling paper and graphite as evolving material state — not as an opacity brush. Pencil marks are deposits into paper height, roughness, binding, loose/bound graphite, compaction, and damage buffers; display tone and surface sheen are derived from that state in CUDA. Physical tools (pencil grades 4H–8B, regular/kneaded/electric erasers, tortillon, fan brush, powder brush, loose graphite) and paper presets are real material parameters. D3D12 presents; CUDA simulates.

## License

This is **source-available, not open source (yet)**. The code is published under the [PolyForm Noncommercial License 1.0.0](LICENSE.md): you're welcome to read it, learn from it, build it, and use it for any noncommercial purpose. Commercial use and redistribution require a license from the author. A paid, pre-built commercial release is planned — if you want to use Graphite commercially in the meantime, get in touch: brown.zac1989@gmail.com.

## Repo Lane

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
