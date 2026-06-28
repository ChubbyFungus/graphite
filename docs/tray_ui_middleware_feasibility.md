# Tray UI Middleware Feasibility

This note evaluates whether the graphite tool tray should stay in ImGui, move into our CUDA/D3D12 engine renderer, or use a prebuilt game-style UI layer.

## Current App Shape

The native app is a Win32 window with:

- a D3D12 swapchain and command queue,
- a CUDA-owned paper/canvas texture shared with D3D12,
- ImGui rendered after the CUDA canvas is copied to the backbuffer,
- Windows pointer and RealTimeStylus input routed through the main window proc.

That means a tray overlay does not need to be part of the CUDA simulation. It only needs to:

- render after the paper,
- block drawing wherever the tray currently sits,
- receive drag/tool-selection input before paper drawing does,
- report selected tool and tray bounds back to the native app.

## Success Criteria

A suitable tray UI layer must support:

- a movable tray object over full-screen paper,
- componentized tray parts: base, sockets, straps, embossed labels, tool sprites,
- precise authored placement rather than hand-tuned ImGui coordinates,
- drag interaction for the tray and potentially individual tools later,
- texture-rich styling for leather/rubber, straps, shadows, and labels,
- native C++ integration without replacing the CUDA/D3D12 paper pipeline,
- reliable input capture so strokes do not draw underneath the tray.

## Candidate Assessment

### RmlUi

RmlUi is the best first candidate.

Why it fits:

- It is C++ native and designed for game/engine UI overlays.
- Its integration model is explicit: the app owns the frame loop, provides rendering/input interfaces, then calls update/render.
- It has HTML/CSS-like layout, images, events, data binding, and drag/drop patterns.
- Its drag tutorial maps well to tray/tool-slot behavior.
- It is open source, which lowers adoption risk for this project.

Main risks:

- The current RmlUi source release checked for this spike has Win32 platform support, but no ready DirectX 12 renderer backend. Available native Windows backend wiring targets Win32 + OpenGL 2, with Vulkan and SDL/GL alternatives also present.
- We would need to write or import a D3D12 `RenderInterface` implementation before RmlUi can render inside our existing swapchain.
- We must adapt image loading so tray assets can use PNG or our existing WIC texture path.
- RmlUi gives us the component system, but it does not design the graphite tray asset model by itself.

Verdict: **still the best open-source UI model candidate, but not a drop-in D3D12 overlay**. The next RmlUi step would be a D3D12 render-interface spike, not tray authoring.

### NoesisGUI

NoesisGUI is a strong professional option.

Why it fits:

- It is designed for real-time/game UI.
- It has polished XAML controls, styling, animation, and data binding.
- It has a serious custom-engine integration guide and SDK samples.

Main risks:

- Middleware licensing and SDK workflow need evaluation.
- It may be heavier than we need for the tray.
- It is likely better for polished app screens than for a small tactile tray overlay unless we want a broader UI platform.

Verdict: **second-pass candidate if RmlUi is not enough or if we want a more commercial UI stack**.

### Coherent Gameface

Gameface is powerful, but heavier.

Why it fits:

- It supports game/real-time apps and has Dx11/Dx12 backend integration.
- It offers HTML/CSS/JavaScript authoring, DevTools, advanced visual effects, and strong frontend workflow.

Main risks:

- Licensing and runtime complexity are likely much larger than RmlUi.
- Bringing a browser-like UI runtime into this graphite app may be more machinery than the tray needs.

Verdict: **viable but probably too heavy for this immediate tray problem**.

### ImGui

ImGui should stay, but not as the product tray layer.

Good uses:

- debug panels,
- engine inspectors,
- calibration controls,
- temporary diagnostic overlays.

Bad fit:

- product-facing physical tray,
- authored component layout,
- embossed labels and material-like styling,
- long-term reusable tool sockets and straps.

Verdict: **keep for development UI, remove from the final tray surface**.

## Recommended Architecture

Use a two-layer model:

1. CUDA/D3D12 paper layer
   - owns graphite marks and paper simulation,
   - fills the window,
   - continues to render as it does now.

2. RmlUi tray overlay
   - renders after the paper copy and before Present,
   - owns tray markup/styles/assets,
   - handles tray dragging and tool selection,
   - exposes tray bounds and selected tool to the native app.

Input routing should be explicit:

- pointer over tray: send to RmlUi, block paper drawing,
- pointer outside tray: send to paper drawing,
- tray moved: newly exposed paper becomes drawable again.

## Minimal Spike

The right next spike is not a full tray rebuild. It is a small integration proof:

1. Add RmlUi through CMake `FetchContent`.
2. Compile RmlUi core with the included Win32 platform support.
3. Implement a narrow D3D12 `RenderInterface` that can draw colored boxes, generated font textures, scissor regions, and PNG-backed textures.
4. Add a minimal `RmlOverlay` wrapper.
5. Render one movable tray-shaped document above the paper.
6. Route pointer input to RmlUi when it is over the overlay.
7. Verify paper drawing is blocked under the overlay and works again after moving it.

If that spike works cleanly, then rebuild the tray as RmlUi components instead of continuing ImGui tray work.

## Spike Result

The first source-level check found that RmlUi does not currently give this app the ready-made D3D12 overlay path we were hoping for. That means the decision is not "RmlUi versus hand-built tray" yet. The real decision is:

- build a custom D3D12 render backend for RmlUi, then use RmlUi for tray components, or
- build a smaller custom tray overlay directly in our renderer.

Because the tray is the only immediate product UI surface with this physical-object behavior, the custom tray overlay may now be the lower-risk first implementation. RmlUi should stay on the table for broader app UI or if we decide the HTML/CSS component model is worth the D3D12 backend work.

## Sources Checked

- RmlUi repository and backend table: https://github.com/mikke89/RmlUi
- RmlUi integration docs: https://mikke89.github.io/RmlUiDoc/pages/cpp_manual/integrating.html
- RmlUi render interface docs: https://mikke89.github.io/RmlUiDoc/pages/cpp_manual/interfaces/render.html
- RmlUi drag tutorial: https://mikke89.github.io/RmlUiDoc/pages/tutorials/dragging.html
- NoesisGUI integration guide: https://www.noesisengine.com/docs/Gui.Core.SDKGuide.html
- Gameface native quick start: https://docs.coherent-labs.com/cpp-gameface/quick_start/quickstartguide_native/
