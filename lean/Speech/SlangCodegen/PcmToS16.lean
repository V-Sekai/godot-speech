import LeanSlang

/-!
# `Speech.SlangCodegen.PcmToS16` — float32 [-1, 1] → int16 PCM (encode)

CHI-101 Phase A. Mirrors the inner loop inside
`SpeechProcessor::_mix_audio` (`speech_processor.cpp:131-135`),
which converts a float32 PCM frame into the int16 little-endian
buffer that gets handed to Opus.

Formula (matching the C++ literally, including the asymmetric scale):

  scaled = sample * 32767.0f
  clamped = max(-32768.0f, min(32767.0f, scaled))
  out[i] = (int)clamped

Notes on the asymmetry vs. the decode side (`PcmFromS16` uses
`/ 32768.0f`):

  * `1.0f * 32767 = 32767` → fits in int16, no clamp needed.
  * `-1.0f * 32767 = -32767` → fits, but the *minimum representable*
    int16 (-32768) is never produced by this encoder. The clamp's
    lower bound of -32768 only fires for floats below -1.0.
  * The round-trip `1.0f -> PcmToS16 -> PcmFromS16` produces
    `32767 / 32768 = 0.999969...`, not `1.0f`. A ~0.000266 dB gain
    reduction on full-scale signals.

Whether this is desired is a design question, not a correctness
question — both `* 32767` (Q15 with symmetric clamp) and `* 32768`
(asymmetric, requires explicit `1.0` overflow handling) are valid
PCM conventions. We pin the current C++ choice as the spec and
record the asymmetry in the decision doc (F3, informational).

One thread per output sample. The clamp uses `max`/`min` rather
than a CLAMP macro because Slang exposes both as intrinsics with
well-defined IEEE 754 behavior for NaN inputs (returns the
non-NaN argument).

## Bindings (set 0)

  0  ConstantBuffer<PcmToS16Params> { uint numSamples; }
  1  StructuredBuffer<float> samplesIn      length = numSamples
  2  RWStructuredBuffer<int> samplesOut     length = numSamples  (int16 zero-or-sign-extended to int32)
-/

namespace Speech.SlangCodegen.PcmToS16

open LeanSlang

private def floatTy : SlangType := .scalar .float
private def uintTy  : SlangType := .scalar .uint
private def intTy   : SlangType := .scalar .int

def shader : SlangShaderModule :=
  { structs :=
      [ { name := "PcmToS16Params"
        , fields := [⟨"numSamples", uintTy, Semantic.none, none, none, .qIn⟩] } ]
  , globals :=
      [ ⟨"params",     .const "PcmToS16Params", Semantic.none, some 0, some 0, .qIn⟩
      , ⟨"samplesIn",  .roBuf floatTy,          Semantic.none, some 1, some 0, .qIn⟩
      , ⟨"samplesOut", .rwBuf intTy,            Semantic.none, some 2, some 0, .qIn⟩ ]
  , functions := [{
      attrs  := [.shaderCompute, .numthreads 64 1 1]
      name   := "main"
      params := [⟨"tid", .vec .uint 3, .svDispatchThreadId, none, none, .qIn⟩]
      body   :=
        [ .declInit uintTy "i" (.member (.var "tid") "x")
        , .ifNoElse (.bin ">=" (.var "i") (.member (.var "params") "numSamples"))
            [ .ret none ]
        , .declInit floatTy "s"
            (.bin "*" (.index (.var "samplesIn") (.var "i")) (.litFloat 32767.0))
        , .declInit floatTy "lo"
            (.call "max" [.un "-" (.litFloat 32768.0), .var "s"])
        , .declInit floatTy "c"
            (.call "min" [.litFloat 32767.0, .var "lo"])
        , .assign (.index (.var "samplesOut") (.var "i"))
            (.call "int" [.var "c"])
        ] }] }

/-- Pinned reference emission. Drift in `LeanSlang.Emit` that affects
    this output trips `native_decide` below. Regenerate after a deliberate
    change via `lake exe emit_shaders /tmp/emit && cat /tmp/emit/pcm_to_s16.slang`. -/
def expected : String :=
"struct PcmToS16Params {
  uint numSamples;
};

[[vk::binding(0, 0)]]
ConstantBuffer<PcmToS16Params> params;
[[vk::binding(1, 0)]]
StructuredBuffer<float> samplesIn;
[[vk::binding(2, 0)]]
RWStructuredBuffer<int> samplesOut;

[shader(\"compute\")] [numthreads(64, 1, 1)]
void main(uint3 tid : SV_DispatchThreadID) {
  uint i = tid.x;
  if ((i >= params.numSamples)) {
    return;
  }
  float s = (samplesIn[i] * 32767.000000);
  float lo = max((-32768.000000), s);
  float c = min(32767.000000, lo);
  samplesOut[i] = int(c);
}"

/-- The pretty-printer matches the pinned reference. -/
example : LeanSlang.emit shader = expected := by native_decide

/-- The kernel's entry-point name is `main`. -/
example : shader.entryPointName = "main" := by native_decide

end Speech.SlangCodegen.PcmToS16
