import LeanSlang

/-!
# `Speech.SlangCodegen.FrameEnergy` — sum-of-squares per 10 ms frame

First kernel ported into the godot-speech Lean → LeanSlang DSL
pipeline. Computes, per 480-sample mono PCM frame at 48 kHz, the
unnormalized energy

  energy[f] = Σ_{s=0..479} (samples[f·480 + s])²

where `samples` is float32 PCM in the range [-1, 1] (the form Godot's
`AudioEffectCapture` already delivers; the s16 path multiplies by
1/32768 once at capture). One thread per output frame.

Why unnormalized: callers want either (a) the raw sum for adaptive
floor tracking, or (b) `sqrt(energy / 480)` for RMS in volume units.
Doing the divide and sqrt here would lock callers out of (a).

Bindings (set 0):

  0  ConstantBuffer<FrameEnergyParams> { uint numFrames; uint samplesPerFrame; }
  1  StructuredBuffer<float>   samples    length ≥ numFrames · samplesPerFrame
  2  RWStructuredBuffer<float> energy     length = numFrames

`samplesPerFrame` is a constant `480` for the godot-speech runtime, but
keeping it as a uniform avoids baking the constant into the kernel and
matches the Saxpby / Spmv idiom in cloth_dynamics. Accumulation is
strict left-to-right fp32 — both the slangc-cpp emit and the CPU
reference in `tests/slang_validate/frame_energy_test.cpp` must perform
the sum in the same order to remain bit-exact.

The pinned reference text below is asserted by `native_decide`. Any
drift in `LeanSlang.Emit` that affects this output trips here. To
regenerate after a deliberate change, run
`lake exe emit_shaders /tmp/emit && cat /tmp/emit/frame_energy.slang`
and replace `expected` with the new output.
-/

namespace Speech.SlangCodegen.FrameEnergy

open LeanSlang

private def floatTy : SlangType := .scalar .float
private def uintTy  : SlangType := .scalar .uint

/-- Per-frame energy kernel. One thread per output frame; each thread
    walks its 480-sample slice in order. -/
def shader : SlangShaderModule :=
  { structs :=
      [ { name := "FrameEnergyParams"
        , fields :=
            [ ⟨"numFrames",       uintTy, Semantic.none, none, none, .qIn⟩
            , ⟨"samplesPerFrame", uintTy, Semantic.none, none, none, .qIn⟩ ] } ]
  , globals :=
      [ ⟨"params",  .const "FrameEnergyParams",  Semantic.none, some 0, some 0, .qIn⟩
      , ⟨"samples", .roBuf floatTy,              Semantic.none, some 1, some 0, .qIn⟩
      , ⟨"energy",  .rwBuf floatTy,              Semantic.none, some 2, some 0, .qIn⟩ ]
  , functions := [{
      attrs  := [.shaderCompute, .numthreads 64 1 1]
      name   := "main"
      params := [⟨"tid", .vec .uint 3, .svDispatchThreadId, none, none, .qIn⟩]
      body   :=
        [ .declInit uintTy  "f" (.member (.var "tid") "x")
        , .ifNoElse (.bin ">=" (.var "f") (.member (.var "params") "numFrames"))
            [ .ret none ]
        , .declInit uintTy  "base"
            (.bin "*" (.var "f") (.member (.var "params") "samplesPerFrame"))
        , .declInit floatTy "acc" (.litFloat 0.0)
        , .forCount "s" (.litUint 0) (.member (.var "params") "samplesPerFrame")
            [ .declInit floatTy "x"
                (.index (.var "samples") (.bin "+" (.var "base") (.var "s")))
            , .assign (.var "acc")
                (.call "fma" [.var "x", .var "x", .var "acc"])
            ]
        , .assign (.index (.var "energy") (.var "f")) (.var "acc")
        ] }] }

/-- Pinned reference emission. Drift in `LeanSlang.Emit` that affects
    this output trips `native_decide` below. Update both this string and
    the kernel in lockstep. -/
def expected : String :=
"struct FrameEnergyParams {
  uint numFrames;
  uint samplesPerFrame;
};

[[vk::binding(0, 0)]]
ConstantBuffer<FrameEnergyParams> params;
[[vk::binding(1, 0)]]
StructuredBuffer<float> samples;
[[vk::binding(2, 0)]]
RWStructuredBuffer<float> energy;

[shader(\"compute\")] [numthreads(64, 1, 1)]
void main(uint3 tid : SV_DispatchThreadID) {
  uint f = tid.x;
  if ((f >= params.numFrames)) {
    return;
  }
  uint base = (f * params.samplesPerFrame);
  float acc = 0.000000;
  for (uint s = 0u; s < params.samplesPerFrame; ++s) {
    float x = samples[(base + s)];
    acc = fma(x, x, acc);
  }
  energy[f] = acc;
}"

/-- The pretty-printer matches the pinned reference. -/
example : LeanSlang.emit shader = expected := by native_decide

/-- The kernel's entry-point name is `main`. -/
example : shader.entryPointName = "main" := by native_decide

end Speech.SlangCodegen.FrameEnergy
