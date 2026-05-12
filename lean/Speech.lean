import Speech.SlangCodegen

/-!
# `Speech` — godot-speech kernels in Lean → LeanSlang DSL

CHI-101 Phase A. Mirrors TOOL_cloth_dynamics's `Cloth` Lean lib: every
deterministic V-Sekai-side speech kernel and the VAD-gating policy are
expressed as a `LeanSlang.SlangShaderModule`, then pinned by
`native_decide` against a reference emit string and bit-exact-tested
against a CPU reference in `tests/slang_validate/`.

Upstream-pinned components (libopus internals, libsamplerate internals,
Silero VAD weights, whisper.cpp transcription path) sit outside this
boundary. Only V-Sekai-side deterministic bits get the Lean treatment.

Submodules live under `Speech.SlangCodegen.*`. First kernels:

* `Speech.SlangCodegen.FrameEnergy` — RMS sum-of-squares per 480-sample
  10 ms frame at 48 kHz. Source signal for energy-based VAD fallback
  and for diagnostics on the captured mic stream.
* `Speech.SlangCodegen.FramingCursor` — capture-buffer framing cursor
  policy: integer division of `(carry + newSamples)` by `frameSize`
  per audio-thread tick. Encodes the safe form of the loop currently
  in `speech_processor.cpp:130`, which uses unsigned subtraction and
  underflows when `resampled_frame_count < frameSize`.
* `Speech.SlangCodegen.VadGate` — hysteresis state machine that turns
  per-frame VAD scores (from Silero) into a gated boolean stream with
  enter/exit thresholds and hangover frames. The classifier itself is
  upstream-pinned; only the gating policy on top is formalized here.
-/
