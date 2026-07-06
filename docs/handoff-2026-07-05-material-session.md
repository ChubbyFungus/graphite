# Handoff — Material/Input Session 2026-07-05

## Fixed and verified this session (probe-measured, tests green)

- Pencil deposit beading at packet spacing: half-open capsule ownership
  (each pixel deposits from exactly one segment). Probe: fast-stroke ripple
  104% -> 23% (residual = paper tooth).
- Per-segment endpoint feather replaced by stroke-arc-length taper
  (`StrokePacket.strokeDistancePx`, set by `GraphiteDocument`).
- Tortillon drag race: non-atomic scatter-writes to neighbor pixels lost
  updates in warp-coherent streaks; now `atomicAdd`.
- ImGui panel was fully unclickable: `EnableMouseInPointer` delivers
  WM_POINTER, ImGui needs legacy mouse messages; UI-bound pointer messages
  now forwarded to `DefWindowProc` for synthesis.
- Packet velocity/speed was never computed on the live path (adapter only
  enriched within one message batch; WM_POINTER delivers ~1 packet/message).
  Now derived cross-packet in `GraphiteDocument` with EMA smoothing. All
  speed-dependent material behavior was dead until this.
- Deposit rates + grade tone capacity re-anchored via headless probe
  (`tmp/probe_stroke_profile.cu`): single pass p=0.5 tone 2H .06 / HB .14 /
  2B .26 / 8B .57; saturation display values 4H .70 / HB .49 / 2B .24 /
  8B .00 (only softest grades reach black).
- Speed-skip dry stroke: fast tip rides tooth peaks; skip grain is finer
  than stroke width and stretched along motion (was severing full line
  width in perpendicular bands).
- Canvas Layers panel folded into F12; hidden hit-zone no longer eats input.
- Headless backend init (no D3D12 handle = material sim only).

## Known remaining work (do not gloss)

1. **Stroke-end taper**: leading-edge feather means stroke ENDS stop square
   (end cap culled for interior continuity). Proper fix: `isStrokeStart` /
   `isStrokeEnd` flags plumbed from `beginStroke`/`endStroke` through
   segment submission; apply feather only at true mark boundaries.
2. **Tortillon residual noise**: stale cross-pixel reads during the same
   launch (neighborTone, source sampling) produce faint nondeterministic
   noise. Proper fix: double-buffer material reads for blend tools.
3. **Fan brush strength**: dose normalization (length/(2*radius)) is
   correct but constants were tuned against the old multi-application bug;
   whole-tool strength multiplier needs a feel pass. Zac verdict pending.
4. **Speed-skip tuning**: current gate (skipTooth threshold 0.42, spread
   5.5, pressure recovery 0.35) anchored by probe, not by eye across
   grades/papers. Fine-tune during calibration pass.
5. **Blunt vs sharpen tip**: not exercised this session; verify tip state
   interacts sanely with new speed-skip and capacity ceilings.
6. **Physical calibration session** (22 swatches) still the real anchor —
   web search 2026-07-05 confirmed no published per-grade darkness dataset
   exists (no industry standard for mark darkness); our protocol is the
   only path to real data.
7. **Probe graduation**: `tmp/probe_stroke_profile.cu` should become a real
   CTest target with ripple/invariance assertions so deposition regressions
   gate the build.

## Verification lane used

nvcc single-TU probe build (see header of `tmp/probe_stroke_profile.cu`),
then full CMake build + 3 CTest suites, then hand test.
