# CUDA-First Native Windows Graphite App PRD

You are building a native Windows drawing/sketching app whose goal is to recreate the feel of graphite pencil on paper as closely as possible.

This is not a toy vertical slice unless explicitly requested. Do not reduce the task to "smallest real CUDA + D3D12 demo." Build toward the actual app architecture.

## Target Hardware

- Windows PC
- NVIDIA GPU present
- Pen-display drawing tablet connected by HDMI/DisplayPort for video and USB for pen input
- The PC runs the app and CUDA simulation
- The tablet is display plus stylus digitizer

## Core Stack

- C++20 native Windows app
- CUDA-first graphite/paper simulation engine
- Direct3D 12 display/presentation
- CUDA to D3D12 interop where appropriate
- Win32 shell first unless the repo already has a better native shell
- Dear ImGui for tool/debug UI
- WM_POINTER baseline pen input
- WinTab support for Wacom/pro pen displays
- No WPF-first rewrite
- No Apple/iPad/Metal path
- No NPU/DirectML features for v1

## Mission

Build the app toward a real CUDA-first graphite sketching system, not a generic paint app.

The app should model graphite and paper as evolving material state:

- Paper height/tooth
- Paper roughness/catch
- Paper sizing/binding
- Bound graphite
- Loose graphite
- Compaction/burnish
- Paper damage/fuzz
- Tool contamination/load
- Display tone
- Surface normals/sheen

## Engine Tool Roadmap

Tools to support in the engine roadmap:

- Graphite pencil
- Regular eraser
- Kneaded eraser
- Tortillon/blending stump
- Fan brush
- Powder brush

## Important Behavior

- Pencil marks are not simple opacity strokes.
- Pressure affects deposition, valley fill, paper compaction, and future tool behavior.
- Paper state changes with repeated strokes.
- Regular eraser removes material aggressively and can damage/flatten tooth.
- Kneaded eraser lifts selectively and can saturate with graphite.
- Tortillon moves and redeposits graphite directionally and compacts the paper.
- Fan brush softly redistributes loose graphite over a wide area.
- Powder brush deposits and moves loose graphite/powder, strongly affected by paper tooth.

## Architecture

- Keep UI, input, engine, CUDA backend, and D3D12 renderer separate.
- CUDA is the primary simulation backend.
- D3D12 owns presentation/rendering.
- UI should control parameters and debug state, not own drawing behavior.
- Input should normalize pen packets before tools consume them.
- Do not scatter raw CUDA calls through UI/input code.
- Do not build an HLSL fallback unless explicitly requested later.

## Expected App Structure

```text
app/
  Win32 entry point
  window/message loop
  app lifecycle

input/
  WM_POINTER adapter
  WinTab adapter
  StrokePacket normalization

engine/
  document/canvas model
  tiled paper state
  tool system
  undo/redo/event log
  backend interface

cuda/
  CudaGraphiteBackend
  CUDA kernels for pencil, erasers, tortillon, brushes, smudge/transport, compositing

render/
  D3D12 device/swapchain
  CUDA-D3D12 interop
  present/composite shaders

ui/
  Dear ImGui panels
  tool settings
  paper presets
  debug buffer visualizers
  performance/input monitors
```

## Input Requirements

`StrokePacket` should preserve:

- Canvas x/y
- Raw screen x/y
- Pressure
- Tilt
- Rotation/orientation if available
- Timestamp
- Velocity or enough data to calculate velocity
- Tip vs eraser
- Barrel button state
- Source API
- Raw device info when available

## Implementation Rules

1. Read the current repo before changing anything.
2. Report the existing architecture honestly.
3. Build toward the real app, not a detached demo.
4. Keep CUDA primary.
5. Use D3D12 for presentation.
6. Use Dear ImGui or an equally appropriate engine-style UI, not WPF.
7. Preserve a path for real pen-display input.
8. Do not add NPU/AI features.
9. Do not reduce the assignment to "smallest vertical slice" unless explicitly requested.
10. When staging work, choose coherent milestones that advance the actual app architecture.

## Output Requirements

When done, report:

- Files inspected
- Current architecture found
- Files changed
- What works now
- What is incomplete
- Whether CUDA is actually used
- Whether D3D12 is actually used
- Whether pen input is real or stubbed
- How to build/run/test
- Next concrete engineering steps
