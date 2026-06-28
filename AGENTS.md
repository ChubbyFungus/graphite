# AGENTS.md instructions for C:\Users\ftbal\graphite

- The user does not want rushed implementation. Do not choose a fast adjacent shortcut when the request implies a real product-quality implementation.
- If the user asks for generated UI, assets, tools, or studio-object interactions, build the actual component/assets/workflow they asked for. Do not use a single background image, placeholder shapes, fake previews, or "visual direction" as a substitute for real implemented UI objects.
- Default to the user's product logic: this app should feel like a real graphite studio, with physical tools and physics-first behavior. If an implementation would only look vaguely similar while violating that logic, stop and correct the approach before coding.
- Speak to the user in logic and systems language rather than code-heavy language.
- Treat `native/Graphite.EngineSlice` as the active app lane. `Graphite.NativeApp` is the source of truth for runtime behavior.
- The browser, Vite/Electron/Rust/WASM, and WPF prototypes were extracted to `C:\Users\ftbal\graphite-non-cuda` on June 9, 2026. Use them only as reference material, not as build or implementation targets.
- `tools/toolkit_layout_editor.html` is a native-asset layout helper, not a browser app lane.
- Start from `README.md` and `native/Graphite.EngineSlice/README.md` before changing runtime behavior. For material calibration work, also read `docs/graphite_material_calibration_protocol.md`.
- Respect `.gitignore`: build output, logs, diagnostics CSVs, realtime stylus status, and generated native UI before-images stay local unless Zac explicitly promotes them as durable evidence.
- For native code or material-rule changes, use the narrow CMake/CTest lane in `native/Graphite.EngineSlice/README.md` or the broad verifier:

```powershell
powershell -ExecutionPolicy Bypass -File tools\verify_native_slice.ps1 -ExpectedCudaToolkitMajorMinor 13.1
```

- If hardware evidence matters, verify with the relevant pen diagnostics or material calibration CSV gate instead of claiming completion from the simulated path alone.
