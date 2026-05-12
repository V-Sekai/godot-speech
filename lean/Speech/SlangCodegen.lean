import Speech.SlangCodegen.FrameEnergy
import Speech.SlangCodegen.FramingCursor
import Speech.SlangCodegen.JitterAppend
import Speech.SlangCodegen.VadGate

/-!
# `Speech.SlangCodegen` — Slang shader codegen umbrella

Each submodule produces a `LeanSlang.SlangShaderModule` for one of the
godot-speech audio-thread kernels. Pinned `native_decide` fixtures
assert the emission text against a hand-checked reference per kernel.

Layout convention (same as `Cloth.SlangCodegen`):

- `shader   : SlangShaderModule`
- `expected : String`
- two `example` lemmas: `emit shader = expected` and
  `shader.entryPointName = "main"`.

We don't run audio on the GPU. Both `slangc-cpp` (production target)
and `slangc-metal` (discipline target) must build clean from each
emitted `.slang` source — the Metal compile is a free portability
check that surfaces hidden CPU assumptions in the spec.
-/
