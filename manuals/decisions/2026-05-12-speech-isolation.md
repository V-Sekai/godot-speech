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
| 2    | Lean spec: jitter-buffer append (receive-side)      | done         |
| 2    | Lean spec: PCM s16↔float (encode + decode)          | done         |
| 2    | Lean spec: PCM framing / resampling (full)          | partial      |
| 2    | Lean spec: Opus encode/decode invariants            | not started  |
| 2    | Lean spec: jitter buffer sizing (C5 from PredictiveBVH) | done     |
| 3    | Dual-target codegen (slangc-cpp + slangc-metal)     | done (both pass) |
| 3    | `tests/slang_validate/` bit-exact CPU validators    | done (4 kernels, green) |
| 4    | Diff `speech_processor.cpp` against Lean reference  | **F1 landed + fixed; F2 noted; F4 landed + fixed** |
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

### F2. `JITTER_BUFFER_SPEEDUP` / `JITTER_BUFFER_SLOWDOWN` are dead settings

**Status:** confirmed by `grep` over `speech.cpp` + `speech.h`. The fields
exist, the getters/setters are bound, and `ADD_PROPERTY` exposes them to the
editor. But **nothing reads them in any decision-logic code path**.

```
$ grep -n 'JITTER_BUFFER_SPEEDUP\|JITTER_BUFFER_SLOWDOWN' speech.cpp speech.h
speech.h:93:    int JITTER_BUFFER_SPEEDUP = 12;
speech.h:94:    int JITTER_BUFFER_SLOWDOWN = 6;
speech.cpp:114-128: getters/setters only
speech.cpp:307-321: ClassDB / ADD_PROPERTY only
```

The accompanying playback-rate fields `STREAM_STANDARD_PITCH` and
`STREAM_SPEEDUP_PITCH` are also exposed but the playback code path doesn't
read either — it always pushes packets at the default rate.

**Implication:** either an adaptive playback-rate scheme was planned and the
detection-side code never landed, or it was ripped out and the configuration
surface was left behind. Either way, scripts setting these properties expect
behavior the engine doesn't actually provide.

**Recommendation:** fold into Phase B step 11 (push-to-talk + open-mic
toggles). The jitter-buffer adaptive speed is a natural addition to the
network-transport story and the Lean spec is straightforward (compare
`currentSize` against thresholds; emit a per-tick rate-adjustment factor).
Until then, mark these properties `@deprecated` or remove them to avoid
silent misconfiguration.

**Status of the JitterAppend kernel for F-finding:** the `JitterAppend`
validator runs all eight oracle cases (first-packet, in-order, gap,
overflow-maxSize, duplicate, out-of-order in-range, out-of-order too-old,
forward-by-1) and the kernel matches both the CPU reference and the
hand-checked oracle frame-for-frame. The `on_received_audio_packet` math
in `speech.cpp` is *correct* — it doesn't have an F1-style bug. The kernel
stays in tree as a regression guard for any future edits to the receive path.

### F3. PCM s16↔float round-trip gain is asymmetric (informational)

**Status:** pinned by `tests/slang_validate/pcm_roundtrip_test.cpp`. **No
fix recommended** — design choice not a bug — but the asymmetry is now
formally pinned so future review can revisit with a concrete number.

The encode side (`PcmToS16`, mirroring the inner loop of
`SpeechProcessor::_mix_audio`) multiplies by **32767** with a clamp to
`[-32768, +32767]`. The decode side (`PcmFromS16`, mirroring
`SpeechProcessor::_16_pcm_mono_to_real_stereo`) divides by **32768**.
Round-tripping `1.0f` produces `0.9999695...`, not `1.0f`:

```
pcm_roundtrip: encode/decode asymmetry sweep (N=65):
  max |err|          = 3.051758e-05  (theoretical at x=+1: 3.051758e-05)
  max signed err     = +3.051758e-05
  round-trip gain    = 0.999969 (= 32767/32768; -0.0003 dB)
```

Both `* 32767` (Q15 with symmetric clamp; this codebase's choice) and
`* 32768` (true 1-LSB-per-step encode; requires explicit overflow
handling for input == 1.0) are valid PCM conventions. `* 32767`
trades the unreachable `-32768` int16 value for a clamp-safe encoder
that handles `1.0f` exactly without overflow. -0.0003 dB of round-trip
gain is well below human-audible threshold (typical JND is ~0.5 dB).

The validator pins the gain at exactly `32767/32768` and fails if the
round-trip of `1.0f` ever returns something else. If a future refactor
adopts `* 32768` (e.g. for tighter Bit-Exact-When-Possible
interoperation with other PCM libraries), the validator will trip and
require the decision to be revisited explicitly.

### F4. C5 / G181 (PredictiveBVH gap class) — voice jitter buffer too shallow for WAN RTT

**Status:** confirmed by Lean spec
`lean/Speech/Protocol/JitterBufferSizing.lean`. **Fixed**: default
`MAX_JITTER_BUFFER_SIZE` raised from `16` (160 ms) to `32` (320 ms).

**Provenance:** voice falls under the conditions established by
V-Sekai-fire's predictive-BVH spec for networking protocols
([PredictiveBVH/Protocol/ScaleContradictionsGapClass.lean](https://github.com/V-Sekai-fire/multiplayer-fabric-predictive-bvh/blob/main/PredictiveBVH/Protocol/ScaleContradictionsGapClass.lean)).
Of the seven adversarial gap classes (C1–C7), the one that applies
directly to a 1D temporal audio stream is **C5 / G181 — Effective delta
exceeded by RTT**:

> *Witness*: configured `δ` < actual `δ` from client RTT → bound the
> spec puts on time budget is violated.
> *Mitigation*: `δ := max(configured, rttTicks)`.

For voice, `δ` is the jitter buffer depth (in 10 ms packets), and
`rttTicks` is client RTT divided by 10 ms. The prior default of 16
packets covers only 160 ms RTT, which fails:

| Route type           | Typical one-way | RTT  | Required depth (packets) | Prior 16 covers? |
|----------------------|-----------------|------|--------------------------|------------------|
| LAN                  | <5 ms           | 10 ms| 1                        | ✓                |
| Continental WAN      | 30–60 ms        | ~100 ms | 10                    | ✓                |
| Transcontinental WAN | 70–150 ms       | ~200 ms | 20                    | ✗ (16 < 20)      |
| Trans-Pacific        | 120–200 ms      | ~300 ms | 30                    | ✗                |
| LEO satellite        | 30–50 ms        | ~80 ms  | 8                     | ✓                |
| GEO satellite        | 250–600 ms      | ~1200 ms| 120                   | ✗                |

The new default of 32 (320 ms) handles most transcontinental WAN. GEO
satellite operators must call `Speech.set_max_jitter_buffer_size(120)`
(or higher) per the C5 mitigation formula. The Lean module includes
`native_decide` examples that prove the prior `16` fails the satellite
case and that the mitigation formula `max(baseline, rttTicks)` is
sound.

**Why C5 is the only gap class that applies:**

* C1 (velocity), C2 (acceleration), C7 (segment-boundary velocity) —
  spatial / motion concerns. Voice has no velocity analog.
* C3 (position discontinuity / teleport) — voice has no spatial
  position; Steam Audio spatialization at the receiver (Phase B,
  CHI-101 step 9) attaches a position later but the *audio path*
  itself is 1D.
* C4 (entity lifecycle gap) — partially applies: the
  peer-join-to-first-packet window. The current `currentSeqValid == 0`
  path in `on_received_audio_packet` handles this with `fillerCount =
  0, appendNew = 1`. A stricter C4 mitigation would pre-allocate a
  silent slot at peer-join time so audio is never cold-started;
  candidate for a follow-up if a real C4 incident shows up.
* C6 (coordinate frame mismatch) — Phase B concern (Steam Audio
  HRTF), not Phase A.

## Test-harness requirement — Godot-independent networking tests

**Standard:** any test that exercises networking-related policy
(jitter buffer, framing cursor, packet sequence math, sizing
invariants, etc.) must build and run **without** `godot-cpp` or
the Godot engine. Existing pattern: `tests/slang_validate/*.cpp` —
hand-written C++ that links only against the slangc-emit, the slang
prelude header, and the C++ standard library.

**Why:** networking policy is verified at the *math* layer (state
machines, integer arithmetic, RTT-vs-depth invariants). Pulling in
the Godot engine to test it would couple test execution to the editor
build (slow, fragile across platforms, blocks CI matrices), and the
real bugs (F1's unsigned underflow, F4's RTT-vs-depth mismatch) live
in pure arithmetic that doesn't need a SceneTree to surface.

**Current state of the test tree:**

| Path                                | Godot-dependent? | What it covers                                                              |
|-------------------------------------|------------------|-----------------------------------------------------------------------------|
| `tests/slang_validate/*.cpp`        | **No**           | Lean-formalized kernels: FrameEnergy, FramingCursor, JitterAppend, VadGate. |
| `tests/test_speech.h`               | Yes (SceneTree)  | Property getters/setters, integration with `AudioStreamPlayer`.             |
| `tests/test_speech_decoder.h`       | Yes              | `SpeechDecoder` end-to-end with `PackedByteArray`.                          |
| `tests/test_speech_processor.h`     | Yes              | `SpeechProcessor::_16_pcm_mono_to_real_stereo`, processor lifecycle.        |
| `tests/test_playback_stats.h`       | Yes              | `PlaybackStats` initialization + stats dictionary.                          |

The Godot-dependent tests cover ABI-and-lifecycle concerns (PR
binding, ref-counted ownership, scene-tree integration) that *do*
require the engine. The math layer should stay independent.

**Going-forward rule for CHI-101 Phase A work:**

* Any new networking policy lands first in
  `lean/Speech/Protocol/` (formal spec) and
  `tests/slang_validate/` (Godot-free C++ validator).
* If a Godot-dependent test is added for the same surface, it
  must be in addition to — not in lieu of — the slang_validate
  harness.
* The slang_validate harness is the *normative* test; any
  divergence between it and a Godot-side test is a finding worth
  recording.

**CI coverage:** the `.github/workflows/godot_free_ci.yml` workflow
runs the Godot-free trees on every PR + main push:

* `Slang validators (ubuntu-latest)` and `Slang validators (macos-latest)` —
  install Lean via elan, install pinned `slangc` v2026.8.1 via
  `misc/install-slang.sh`, build the Lean tree (`native_decide`
  pins), then run `make -C tests/slang_validate test-cpp`. The
  macOS job additionally runs `metal-discipline` (slangc-metal →
  xcrun metal → .metallib) so the dual-target discipline check
  doesn't rot.
* `Audio model (macOS, CoreAudio)` — `cmake --build` the
  `tests/godot_audio_model/` tree on macOS (including the verbatim
  CoreAudio driver) and run the smoke test.

No `godot-cpp`, no Godot module build, no scons. Total CI surface
matches the test trees in the standard above. Both check names
should be added to `main`'s branch protection as required checks
once the workflow has run at least once.

**Follow-up candidates** for porting Godot-side state-machine logic
into Godot-free slang_validate validators:

* `attempt_to_feed_stream` packet-feed cadence (currently exercised
  only via the SceneTree-bound integration test).
* The RTT-vs-jitter-buffer-depth invariant from F4 (currently has a
  Lean proof but no runtime validator — adding one would close the
  loop the same way `framing_cursor_test` closed it for F1).

### End-to-end network test — basis from V-Sekai-fire/multiplayer-fabric-godot

For a real send/receive loop that exercises the voice path under live
QUIC/WebTransport conditions (RTT, jitter, datagram loss) — the
substrate to reuse is V-Sekai-fire's existing HTTP/3 + WebTransport
module:

[`V-Sekai-fire/multiplayer-fabric-godot/modules/http3 @ 1c3e475`](https://github.com/V-Sekai-fire/multiplayer-fabric-godot/tree/1c3e475a4cd116f0b3ca13b25e4236bed783be93/modules/http3)

Contents of interest:

* `quic_picoquic_backend.{cpp,h}` — picoquic-library wrapper that
  does the actual QUIC transport work. The wrapper layer is thin;
  picoquic itself is portable C with no Godot dependency.
* `web_transport_peer.{cpp,h}` — Godot `MultiplayerPeer` adapter
  mapping `TRANSFER_MODE_UNRELIABLE`/`UNRELIABLE_ORDERED` to QUIC
  datagrams (the path voice uses) and `TRANSFER_MODE_RELIABLE` to
  per-packet bidi streams.
* `lean/WebTransport.lean` — Lean audit of the state machine
  (acyclicity, queue discipline, datagram-reader exclusivity).

**Test architecture**: a standalone test binary that links picoquic
+ a slim non-Godot wrapper around the QUIC-datagram subset of
`quic_picoquic_backend`, spins up two endpoints on the loopback,
sends real Opus-encoded voice frames through, and validates that
the receive-side jitter buffer behaves per the F4 invariants under
configurable RTT/jitter/loss profiles. The Godot side stays the
shipping `web_transport_peer`; only the test driver is Godot-free.

This reuses the QUIC + Lean work already done rather than vendoring
another QUIC stack, while keeping the test outside the Godot build
matrix per the standard above. Implementation is a separate work
item — this entry only documents the basis and direction.

### Godot audio-system model — minimum surface the test binary must reproduce

The standalone `tests/net_loopback/` binary needs a stand-in implementation of
the slice of Godot's audio API that `Speech`, `SpeechProcessor`, and
`SpeechDecoder` link against. Authoritative reference for the API shapes is
the engine source at `/Users/ernest.lee/Desktop/multiplayer-fabric/godot`
(specifically `servers/audio/`, `servers/audio/effects/`, and `scene/audio/`).

**Surface enumeration** (every symbol godot-speech actually uses):

| Symbol                                            | Usage                                                            |
|---------------------------------------------------|------------------------------------------------------------------|
| `AudioServer::get_singleton()`                    | Bus lookup, input mix rate                                       |
| `AudioServer::get_bus_index(StringName)`          | `set_streaming_bus` — find bus by name                           |
| `AudioServer::get_bus_effect_count(int)`          | Iterate effects on a bus                                         |
| `AudioServer::get_bus_effect(int, int)`           | Get the `AudioEffectCapture` ref                                 |
| `AudioServer::get_input_mix_rate()` / `AudioDriver::get_singleton()->get_input_mix_rate()` | Capture rate                                                     |
| `AudioEffectCapture::get_buffer(int)`             | Pull captured stereo frames from the ring                        |
| `AudioEffectCapture::get_buffer_length_frames()`  | Ring capacity                                                    |
| `AudioEffectCapture::get_frames_available()`      | Current ring fill                                                |
| `AudioStreamGenerator::set_mix_rate(float)`       | Configure playback rate                                          |
| `AudioStreamGenerator::get_mix_rate() / get_buffer_length()` | Playback ring sizing                                  |
| `AudioStreamGeneratorPlayback::push_buffer(PackedVector2Array)` | Push decoded audio to the speaker                       |
| `AudioStreamGeneratorPlayback::get_frames_available()` | How much room is left in the ring                          |
| `AudioStreamGeneratorPlayback::get_skips()`       | Underrun counter                                                  |
| `AudioStreamPlayer` / `AudioStreamPlayer2D` / `AudioStreamPlayer3D` | Node container; called via `cast_to`, `has_method`, `call("play", ...)`, `call("get_stream_playback")` |
| `Node` polymorphism                               | `get_node_or_null`, `queue_free`, parent/child                   |
| Core types: `Vector2`, `PackedVector2Array`, `PackedByteArray`, `PackedFloat32Array`, `Dictionary`, `Array`, `Variant`, `Ref<T>`, `String`, `StringName`, `NodePath`, `Mutex` | Pervasive                                                |
| Math helpers: `MAX`, `CLAMP`, `Math::abs`, `Math::is_zero_approx` | A handful of call sites                                         |

**Design constraints** (going into the model):

* **Same public signatures, in-memory storage.** The model implements the
  same class names and method signatures Godot exposes, but each method's
  body operates on plain `std::vector`-backed buffers. No threading, no
  audio driver, no DSP — just enough state to make `Speech`'s capture/
  receive loops run end-to-end.
* **Synthetic time.** The test driver ticks the model explicitly (e.g.
  `model.advance_audio_thread_by(N frames)`) rather than relying on a
  real audio clock. Predictable, single-threaded, no jitter except what
  the test driver injects.
* **Single-precision throughout.** Matches the engine and avoids any
  fp32-vs-fp64 drift in spec ↔ runtime comparisons.
* **Built independently of godot-cpp.** The model is its own library
  (`tests/godot_audio_model/`), pulled into the net_loopback binary
  via CMake — not scons, since scons assumes a Godot module build.

**Where the model fits in the test stack:**

```
tests/net_loopback/main.cpp                ← ImGui frame loop + test driver
        │
        ├── tests/godot_audio_model/       ← stand-ins for AudioServer,
        │     ├── audio_server.{h,cpp}        AudioEffectCapture,
        │     ├── audio_effect_capture.{h,cpp}AudioStreamGenerator(Playback),
        │     ├── audio_stream_generator.{h,cpp} core types (Vector2,
        │     ├── core_types.h                PackedVector2Array, Ref, etc.)
        │     └── CMakeLists.txt
        │
        ├── ../slang_validate/*_emit.cpp   ← live kernels (FrameEnergy,
        │                                     FramingCursor, JitterAppend,
        │                                     VadGate, PcmToS16, PcmFromS16)
        │
        └── thirdparty/picoquic_wrapper/   ← slim non-Godot QUIC backend
                                              extracted from V-Sekai-fire's
                                              http3 module @ 1c3e475
```

The model is **link-compatible** with `speech.cpp`, `speech_processor.cpp`,
and `speech_decoder.cpp` so the test binary can run the *real* voice
pipeline (capture → encode → QUIC datagrams → decode → playback ring),
not a re-implementation. The slang_validate kernels remain the
normative reference for the math layer; the audio model just provides
the storage substrate the engine code path is built against.

**Implementation outline (separate PR(s)):**

1. **Core types pass + verbatim CoreAudio driver.** `Vector2`,
   `PackedVector2Array`, `PackedByteArray`, `PackedFloat32Array`,
   `Vector<T>`, `Ref<T>`, `RefCounted`, `String`, `StringName`,
   `NodePath`, `PackedStringArray`, `Mutex`, `MutexLock`,
   `SafeNumeric<T>`, math helpers, allocator macros, error macros.
   Header-only except for the `AudioDriver::singleton` definition.
   The engine's CoreAudio driver (`audio_driver_coreaudio.{h,mm}`,
   ~860 lines) is copied verbatim and wired up via header-path
   shims (`shims/servers/audio/audio_server.h`, `shims/core/config/`,
   `shims/core/os/`, `shims/core/math/`) so the copy itself stays
   unedited. CMake build produces `libgodot_audio_model.a` and a
   `godot_audio_model_smoke` exe that constructs the driver and
   reports name + speaker mode. **Done in PR #19.**
2. **AudioEffectCapture + AudioServer bus tree.** `AudioFrame`
   (the engine's stereo float pair); `RingBuffer<T>` (power-of-two
   circular); `AudioEffect` + `AudioEffectInstance` minimal bases;
   `AudioEffectCapture` with `get_buffer`, `can_get_buffer`,
   `get_buffer_length_frames`, `get_frames_available`,
   `clear_buffer`, `set_buffer_length`/`get_buffer_length` and the
   `pushed_frames`/`discarded_frames` counters; `AudioServer` with
   `get_bus_index`, `get_bus_effect_count`, `get_bus_effect`,
   `get_input_mix_rate`. Test-only entries (`push_test_frames` on
   AudioEffectCapture, `test_add_bus`/`test_add_bus_effect` on
   AudioServer) let the harness drive the capture path without an
   audio thread. Smoke test now exercises a 100-frame round-trip
   through the capture ring, asserting bit-exact preservation.
   **Done in PR #22.**
2. **Node + RefCounted stand-ins.** Minimal `Node` with name/parent/child;
   `RefCounted` with strong+weak counts. Enough for `cast_to` to work.
3. **AudioServer + bus model.** Plain map<StringName,int> of bus indices,
   each bus owns a small `vector<Ref<AudioEffect>>`.
4. **AudioEffectCapture model.** Ring of `Vector2` frames; `get_buffer(n)`
   pops the front `n`; test driver pushes synthetic mic audio via a
   non-Godot helper method.
5. **AudioStreamGenerator(Playback) model.** Output ring of `Vector2`;
   `push_buffer` appends with full-ring overflow tracking the same way
   the engine does (`get_skips()` counts overruns).
6. **AudioStreamPlayer + AudioStreamPlayer2D + AudioStreamPlayer3D
   wrappers.** Holds a `Ref<AudioStreamGenerator>` and answers the
   `call("get_stream_playback")` path.
7. **Compile speech.cpp / speech_processor.cpp / speech_decoder.cpp
   against the model.** Verify the same C++ source links against the
   stand-in. Run the existing slang_validate validators against the
   live runtime to confirm parity.

Each pass is its own PR. Total scope is meaningful (several thousand
LOC of stand-in headers) but bounded — the surface table above is the
exhaustive list of what godot-speech actually uses.

### Test UI — Dear ImGui

The standalone networking-test binary uses [Dear ImGui](https://github.com/ocornut/imgui)
(`ocornut/imgui`) as its UI layer. Pin against tag **v1.92.8** (released
2026-05-12). Rationale:

* **Godot-free.** ImGui has no dependency on a scene graph or engine
  runtime — it's a single-header-style C++ library that renders against
  a graphics backend you choose. Keeps the test binary outside the
  Godot build matrix per the standard above.
* **Immediate-mode is right for developer tools.** The whole UI is
  state-driven from the test driver each frame; no widget tree to
  manage means a tight loop between network state and what's on
  screen. Good fit for live-tweaking RTT/jitter/loss profiles and
  watching the jitter-buffer-depth, energy, and VAD-gate signals
  evolve in real time.
* **Cross-platform without effort.** Provided imgui_impl_glfw +
  imgui_impl_opengl3 (or sdl2/vulkan if Steam Audio later prefers a
  different backend) build clean on macOS, Linux, and Windows from
  the same source tree.

**Test UI surface (sketch):**

* **Network profile** — sliders for RTT, jitter σ, datagram loss %,
  reorder window. Buttons for `LAN` / `WAN` / `Trans-Pacific` /
  `GEO Satellite` presets matching the F4 routing table.
* **Live signals** — line charts for jitter-buffer fill depth,
  per-frame energy from `FrameEnergy`, gated boolean from `VadGate`,
  packets-emitted-per-tick from `FramingCursor`.
* **Counters** — `received / decoded / blank-pushed / dropped` per
  peer; sequence-ID gaps; current carry from the framing cursor;
  current jitter-buffer carry-state (`currentSeq`, `currentSize`)
  from `JitterAppend`.
* **Per-kernel Lean-vs-runtime divergence indicator** — if a
  validator's slang_validate harness disagrees with the live
  kernel output on the same input, light up the corresponding
  kernel row.

Library + backend layout (planned):

```
tests/net_loopback/                       # new top-level test binary
├── main.cpp                              # ImGui frame loop + test driver
├── ui_*.cpp                              # one UI surface per kernel/signal
├── thirdparty/imgui/                     # vendored ImGui v1.92.8
│   ├── imgui.{h,cpp}                     # core
│   ├── imgui_demo.cpp                    # demo (debug aid; not always linked)
│   ├── imgui_draw.cpp, imgui_widgets.cpp,
│   │   imgui_tables.cpp
│   ├── backends/imgui_impl_glfw.{h,cpp}
│   └── backends/imgui_impl_opengl3.{h,cpp}
└── thirdparty/picoquic_wrapper/          # slim non-Godot wrapper
                                          #   pulled from V-Sekai-fire's http3 module
```

`ImPlot` (`epezent/implot`) is the natural follow-up if the line
charts get fiddly with raw ImGui — defer that pick until the basic
UI is up.

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
