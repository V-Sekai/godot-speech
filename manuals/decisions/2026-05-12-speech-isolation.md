# 2026-05-12 — Speech isolation under Lean → LeanSlang verification

**CHI-101 Phase A** — formalize the godot-speech kernels and the VAD-gating policy in
isolation, before any multiplayer-fabric integration. This doc tracks Phase A status,
methodology, and outstanding questions; it will be the artifact CHI-101 Step 6 expects.

## Status

| Step | Item                                                | State        |
|------|-----------------------------------------------------|--------------|
| 1    | `lean/` subdirectory mirroring `TOOL_cloth_dynamics`| done         |
| 2    | Lean spec: VAD gating policy                        | done (1st kernel) |
| 2    | Lean spec: frame energy (VAD fallback signal)       | done         |
| 2    | Lean spec: PCM framing / resampling                 | not started  |
| 2    | Lean spec: jitter buffer arithmetic                 | not started  |
| 2    | Lean spec: Opus encode/decode invariants            | not started  |
| 3    | Dual-target codegen (slangc-cpp + slangc-metal)     | done (both pass) |
| 3    | `tests/slang_validate/` bit-exact CPU validators    | done (2 kernels, green) |
| 4    | Diff `speech_processor.cpp` against Lean reference  | not started  |
| 5    | End-to-end VAD-gate test on recorded clips          | not started  |
| 6    | 30-min Win/macOS/Linux audio-thread soak            | not started  |

Phase B (multiplayer-fabric / WebTransport / Steam Audio integration) is gated on
Phase A producing a clean `speech_processor`/Lean diff or a documented fallback
decision.

## Methodology — what we're copying from `TOOL_cloth_dynamics`

The cloth-dynamics work proved the **Lean → LeanSlang DSL → slangc-cpp bit-exact
validator** pattern on the PR-G AVBD adjoint chain: a 1068× drift at step 1, masked
for weeks by visual eyeballing of the simulation, surfaced immediately once the
forward and backward kernels were re-derived in Lean and the analytic adjoint was
finite-difference-checked against the forward emit. Reference:
`/Users/ernest.lee/Desktop/TOOL_cloth_dynamics/lean/Cloth/SlangCodegen/`.

The pattern, applied here:

1. **Each deterministic V-Sekai-side kernel** (one per `Speech.SlangCodegen.*`
   module) is written as a `LeanSlang.SlangShaderModule` AST.
2. **A pinned `expected : String`** holds the exact emit output. `native_decide`
   proves `LeanSlang.emit shader = expected` at Lean build time. Any drift in
   either the kernel or the LeanSlang pretty-printer trips compilation.
3. **`lake exe emit_shaders <dir>`** writes every `.slang` file to disk for the
   downstream slangc pipeline.
4. **`tests/slang_validate/Makefile`** runs each emitted `.slang` through
   `slangc -target cpp` to get a CPU function, links it with a hand-written
   `<kernel>_test.cpp` harness that populates inputs from a known fixture and
   asserts the output matches an **independent CPU reference**.
5. **Both `slangc-cpp` and `slangc-metal` targets must build clean** for every
   kernel. We don't run audio on the GPU, but the Metal build is a free
   second-pair-of-eyes on spec portability: anything that depends on a
   CPU-specific assumption (alignment, intrinsic name, addrspace) won't survive
   translation to a compute shader.

Upstream-pinned components sit **outside** this dual-target boundary:

* `thirdparty/opus/` — libopus internals (NSQ, CELT, SILK encoders): treated as
  a black box. We formalize the **wrapper arithmetic** (frame-size
  bookkeeping, packet-size bounds) but not the codec's internals.
* `thirdparty/libsamplerate/` — same treatment: wrapper arithmetic only.
* Silero VAD ML weights (via `godot-whisper`) — black box producer of per-frame
  scores; only the gating policy on top is formalized.
* whisper.cpp transcription path — disabled at runtime, never crosses the Lean
  boundary.

## What's in `lean/` today

```
lean/
├── lakefile.lean                          # require LeanSlang v0.0.5
├── lean-toolchain                         # leanprover/lean4:v4.29.1
├── Speech.lean                            # top-level lib (imports SlangCodegen)
├── Speech/SlangCodegen.lean               # umbrella module
├── Speech/SlangCodegen/FrameEnergy.lean   # per-frame Σ x² (energy-VAD fallback)
├── Speech/SlangCodegen/VadGate.lean       # hysteresis state machine
└── EmitShaders.lean                       # `lake exe emit_shaders <dir>`
```

`lake build Speech` and `lake build emit_shaders` both succeed clean as of this
doc's date. Both bit-exact `native_decide` pins pass.

`make -C tests/slang_validate test` (with `SLANGC` pointed at a slangc 2026.8+
install — cloth_dynamics' `bin/slangc` works directly) is also green end-to-end:

```
frame_energy: 3/3 frames OK (max_abs_diff=0, 0.0ms)
vad_gate: 20/20 frames OK (one-shot, ref, oracle, split, all match; 0.0ms)
all kernels OK (cpp target)
all kernels OK (metal target — discipline build)
```

Three orthogonal checks per kernel, all required to pass:

1. **Lean `native_decide`** — `LeanSlang.emit shader = expected` proved at
   Lean build time. Catches any drift in either the kernel AST or the
   LeanSlang pretty-printer.
2. **slangc-cpp bit-exact harness** — slangc compiles the emitted `.slang`
   to a C++ function; a hand-written `<kernel>_test.cpp` runs it on a known
   input and compares against an in-order CPU reference for 0-ULP bit
   equality. For `vad_gate`, also checks against a hand-checked oracle and
   against a split-dispatch state-carry-over run.
3. **slangc-metal discipline build** — slangc compiles the same `.slang`
   to a Metal compute shader and Apple's `xcrun metal` + `xcrun metallib`
   produce a `.metallib` artifact. Never dispatched at runtime; failure
   here means the spec leaked a CPU-specific assumption.

## Open questions for Phase A

### Q1. Where does the existing `speech_processor` divergence live?

The CHI-101 issue notes "past V-Sekai voice attempts got stuck on the speech
side". Phase A step 4 is to diff each Lean-specified kernel against the C++
code path in `speech_processor.cpp` / `speech.cpp` / `speech_decoder.cpp` and
find where they disagree.

Hypotheses worth ruling out first:

* **Resampler offset accounting.** `speech_processor.cpp` runs libsamplerate
  with a manual offset increment after each `_resample_audio_buffer` call. If
  the increment formula doesn't match libsamplerate's internal frame consumption,
  capture drift accumulates over minutes.
* **Capture ring-buffer accounting.** `capture_discarded_frames` /
  `capture_pushed_frames` / `capture_ring_current_size` — if any of these are
  updated in the wrong order under audio-thread contention, frames get
  silently dropped.
* **Opus 10 ms vs 20 ms frame mismatch.** `SPEECH_SETTING_MILLISECONDS_PER_PACKET
  = 10` and `OPUS_APPLICATION_VOIP` — verify libopus is configured for 10 ms
  frame size everywhere; one place defaulting to 20 ms would corrupt every
  packet.

None of these are confirmed yet — listing as candidates for the diff pass.

### Q2. VAD policy parameters — what defaults?

The `VadGate` Lean spec parameterizes:

* `enterThreshold` — score above this trips "on". Default candidate: **0.6**.
* `exitThreshold` — score above this refreshes hangover. Default candidate:
  **0.4** (well below enter, so transients don't flap).
* `hangoverFrames` — frames to hold "on" after a dip. Default candidate:
  **8 frames = 80 ms** at 10 ms/frame. WebRTC's open-mic VAD uses ~100 ms in
  most reference configs; 80 ms is on the snappier end and avoids cutting off
  the tail of soft consonants without holding the gate open through
  multi-second pauses.

These should be **validated against recorded clips** in Phase A step 5 before
being baked in as runtime constants.

### Q3. Where does the VAD score actually come from?

The CHI-101 plan uses Silero from `godot-whisper`. Open follow-up: pick the
GDExtension API surface that returns per-frame scores (whisper.cpp's bundled
Silero exposes a `vad_segments` query; we want the raw per-frame probability,
not the segment-merged output, so we can run our own hysteresis on top).

Worst case: vendor the Silero ONNX session into godot-speech directly so we
don't have to wait on a godot-whisper API change.

### Q4. Should we add an `EnergyVadFallback` kernel?

If the Silero integration slips, an energy-floor-based VAD is a poor-but-fast
fallback. The `FrameEnergy` kernel already produces the input; a second kernel
that adapts a noise floor and gates on `energy[f] > k · floor` would be a few
lines on top. Decision deferred until Phase A step 3 outcomes are clear.

## If Phase A fails

Per the CHI-101 issue: drop godot-speech, use stock Godot `AudioEffectCapture`
plus a userland libopus GDExtension. If that's still shaky, voice falls back to
Stage 1 and Stage 0 ships as multi-user dots only.

The decision lives **here** in this doc — at the bottom of Phase A — once the
diff-against-spec pass is complete.

## Pointers

* CHI-101 issue: <https://linear.app/chibifire/issue/CHI-101>
* Methodology reference: `/Users/ernest.lee/Desktop/TOOL_cloth_dynamics/lean/Cloth/SlangCodegen/`
* Pinned-emit pattern: `Cloth/SlangCodegen/SpringProject.lean` (smallest readable example)
* Multi-branch pattern: `Cloth/SlangCodegen/SelfCollisionScan.lean` (`.ifThen`, `.forCount`)
* Validator harness reference: `TOOL_cloth_dynamics/tests/slang_validate/spring_project_test.cpp`
