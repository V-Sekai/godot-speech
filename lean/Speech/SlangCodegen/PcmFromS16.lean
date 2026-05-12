import LeanSlang

/-!
# `Speech.SlangCodegen.PcmFromS16` — int16 PCM → float32 in [-1, 1)

CHI-101 Phase A. Mirrors `SpeechProcessor::_16_pcm_mono_to_real_stereo`
(`speech_processor.cpp:200`), which is the receive-side decode of a
mono int16 PCM packet into the float32 stereo audio that
`AudioStreamGeneratorPlayback::push_buffer` consumes.

Formula:

  out[i] = (float)s16[i] / 32768.0f

Range: s16 ∈ [-32768, +32767] maps to float ∈ [-1.0, +0.999969...).
The asymmetry with the encode kernel (`PcmToS16`, which uses
`* 32767.0f`) is recorded as informational finding F3 in
`manuals/decisions/2026-05-12-speech-isolation.md`.

This kernel is the **mono → mono** half — the duplicate-to-stereo
expansion in the C++ is a trivial copy that doesn't need its own
kernel. Callers can either:
  * Run this kernel + a separate stereo-expand (write L=R=value
    via a `RWStructuredBuffer<float2>` output), or
  * Write the receiver-side HRTF stack on top of mono and let
    Steam Audio handle spatialization at the listener.

CHI-101 picks the second option (Steam Audio receives mono, places
it at the sender's head pose), so we only need mono output here.

One thread per output sample. Bit-exact bound: the result is
`(float)(s16) / 32768.0f` which is exact under IEEE 754 round-to-
nearest because 32768 is a power of two; the division is just a
binary-exponent shift.

## Bindings (set 0)

  0  ConstantBuffer<PcmFromS16Params> { uint numSamples; }
  1  StructuredBuffer<int>     samplesIn      length = numSamples  (int16 zero-or-sign-extended to int32)
  2  RWStructuredBuffer<float> samplesOut     length = numSamples
-/

namespace Speech.SlangCodegen.PcmFromS16

open LeanSlang

private def floatTy : SlangType := .scalar .float
private def uintTy  : SlangType := .scalar .uint
private def intTy   : SlangType := .scalar .int

def shader : SlangShaderModule :=
  { structs :=
      [ { name := "PcmFromS16Params"
        , fields := [⟨"numSamples", uintTy, Semantic.none, none, none, .qIn⟩] } ]
  , globals :=
      [ ⟨"params",     .const "PcmFromS16Params", Semantic.none, some 0, some 0, .qIn⟩
      , ⟨"samplesIn",  .roBuf intTy,              Semantic.none, some 1, some 0, .qIn⟩
      , ⟨"samplesOut", .rwBuf floatTy,            Semantic.none, some 2, some 0, .qIn⟩ ]
  , functions := [{
      attrs  := [.shaderCompute, .numthreads 64 1 1]
      name   := "main"
      params := [⟨"tid", .vec .uint 3, .svDispatchThreadId, none, none, .qIn⟩]
      body   :=
        [ .declInit uintTy "i" (.member (.var "tid") "x")
        , .ifNoElse (.bin ">=" (.var "i") (.member (.var "params") "numSamples"))
            [ .ret none ]
        , .assign (.index (.var "samplesOut") (.var "i"))
            (.bin "/"
              (.call "float" [.index (.var "samplesIn") (.var "i")])
              (.litFloat 32768.0))
        ] }] }

/-- Pinned reference emission. Drift in `LeanSlang.Emit` that affects
    this output trips `native_decide` below. Regenerate after a deliberate
    change via `lake exe emit_shaders /tmp/emit && cat /tmp/emit/pcm_from_s16.slang`. -/
def expected : String :=
"struct PcmFromS16Params {
  uint numSamples;
};

[[vk::binding(0, 0)]]
ConstantBuffer<PcmFromS16Params> params;
[[vk::binding(1, 0)]]
StructuredBuffer<int> samplesIn;
[[vk::binding(2, 0)]]
RWStructuredBuffer<float> samplesOut;

[shader(\"compute\")] [numthreads(64, 1, 1)]
void main(uint3 tid : SV_DispatchThreadID) {
  uint i = tid.x;
  if ((i >= params.numSamples)) {
    return;
  }
  samplesOut[i] = (float(samplesIn[i]) / 32768.000000);
}"

/-- The pretty-printer matches the pinned reference. -/
example : LeanSlang.emit shader = expected := by native_decide

/-- The kernel's entry-point name is `main`. -/
example : shader.entryPointName = "main" := by native_decide

end Speech.SlangCodegen.PcmFromS16
