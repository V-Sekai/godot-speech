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
| 2    | Lean spec: framing cursor (capture-loop policy)     | done         |
| 2    | Lean spec: PCM framing / resampling (full)          | partial      |
| 2    | Lean spec: jitter buffer arithmetic                 | not started  |
| 2    | Lean spec: Opus encode/decode invariants            | not started  |
| 3    | Dual-target codegen (slangc-cpp + slangc-metal)     | done (both pass) |
| 3    | `tests/slang_validate/` bit-exact CPU validators    | done (3 kernels, green) |
| 4    | Diff `speech_processor.cpp` against Lean reference  | **1st finding landed** |
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

## Findings (Phase A step 4)

### F1. `speech_processor.cpp:130` — unsigned-underflow + off-by-one in the framing loop

**Status:** confirmed by `tests/slang_validate/framing_cursor_test.cpp`.
Five concrete diff cases between the Lean spec (`FramingCursor`) and the
literal C++ loop.

**Location:** `speech_processor.cpp:130`, inside `SpeechProcessor::_mix_audio`:

```cpp
while (capture_real_array_offset
       < resampled_frame_count - SPEECH_SETTING_BUFFER_FRAME_COUNT) {
  // emit one 480-sample packet
  capture_real_array_offset += SPEECH_SETTING_BUFFER_FRAME_COUNT;
}
```

**Two distinct defects in one line:**

1. **Unsigned underflow.** `resampled_frame_count` and `SPEECH_SETTING_BUFFER_FRAME_COUNT`
   are both `uint32_t`. When `resampled_frame_count < 480` — which can happen on
   the very first audio-thread tick, or on a tick where `_resample_audio_buffer`
   returns 0 from libsamplerate due to insufficient input — the subtraction wraps
   to ~4 G and the loop reads way past `capture_real_array.size() = 8192`.
   The validator caps iterations at 1 M so we can report the bug without hanging
   CI; in production this would have been an out-of-bounds read until the AV
   subsystem crashed or the audio thread starved.
2. **Off-by-one at the boundary.** Even in the safe range, strict `<` rather than
   `<=` mis-fires whenever `resampled_frame_count - capture_real_array_offset`
   equals exactly `frameSize`. Validator demonstrates kernel=1, buggy=0 for
   the `0 + 480` and `240 + 240` cases. Under happy-path 48 kHz capture with
   `RECORD_MIX_FRAMES = 2048` this boundary rarely lines up — explaining why
   the bug went undetected through manual testing — but every exact-multiple
   tick is silently one frame light.

**Validator output:**

```
framing_cursor diff against speech_processor.cpp:130 loop:
  underflow: 0+100             kernel=0  buggy=1000001(FUSE-CAPPED)  expected=0  DIFF
  underflow: 50+0              kernel=0  buggy=1000001(FUSE-CAPPED)  expected=0  DIFF
  off-by-one: 0+480            kernel=1  buggy=0  expected=1  DIFF
  off-by-one: 240+240          kernel=1  buggy=0  expected=1  DIFF
  happy: 0+960                 kernel=2  buggy=1  expected=2  DIFF
```

**Recommended fix:** replace the loop with explicit integer divmod, mirroring
the kernel:

```cpp
const uint32_t total = capture_real_array_offset + resampler_output_count;
const uint32_t frames_to_emit = total / SPEECH_SETTING_BUFFER_FRAME_COUNT;
for (uint32_t f = 0; f < frames_to_emit; ++f) {
  // emit one 480-sample packet starting at capture_real_array + f * 480
}
capture_real_array_offset = total % SPEECH_SETTING_BUFFER_FRAME_COUNT;
```

That's exactly the policy `FramingCursor.lean` encodes. The fix is deferred to
Phase A step 4b (apply-the-diff pass) and tracked separately — this doc only
records the diagnosis. The kernel + validator stay in tree as a regression
guard.

**Corroborating evidence:** `speech_processor.cpp:401` (the analogous loop
inside `test_process_mono_audio_frames`, the test-only entry point) already
uses the safe form:

```cpp
while (current_offset + SPEECH_SETTING_BUFFER_FRAME_COUNT <= resampled_frame_count) {
```

i.e. the codebase had *already diverged*: someone wrote the safe form for the
test path but did not propagate the fix back to `_mix_audio`. The existing
test suite (`tests/test_speech_processor.h::Test Direct Audio Processing via
test_process_mono_audio_frames`) exercises *only* the test path, so it would
have continued reporting green forever without catching F1.

**Fix applied:** see the diff that landed alongside this doc update —
`_mix_audio` now uses the integer-divmod form, matching both the kernel and
the previously-divergent test path. The `framing_cursor` validator stays in
tree as a regression guard.

**Status of the historical-stickiness hypothesis:** this is *one* of the things
that was wrong on the speech path. Not necessarily the whole story. Phase A
continues by formalizing the next layer (jitter buffer / decode-side cursor)
and diffing those.

## Other hypotheses still open

* **Capture ring-buffer accounting.** `capture_discarded_frames` /
  `capture_pushed_frames` / `capture_ring_current_size` — if any of these are
  updated in the wrong order under audio-thread contention, frames get
  silently dropped. Not yet formalized.
* **Opus 10 ms vs 20 ms frame mismatch.** `SPEECH_SETTING_MILLISECONDS_PER_PACKET
  = 10` and `OPUS_APPLICATION_VOIP` — verify libopus is configured for 10 ms
  frame size everywhere; one place defaulting to 20 ms would corrupt every
  packet. Not yet formalized.
* **`RESAMPLED_BUFFER_FACTOR = sizeof(int)`** in `speech_processor.cpp:44` —
  using `sizeof(int)` as a buffer-size multiplier is a code smell of a
  refactor-gone-wrong. It happens to be `4` on every target platform, so the
  buffer is correctly oversized for 4× upsampling, but the *intent* is opaque.
  Worth a follow-up rename even if not load-bearing.

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
