import LeanSlang

/-!
# `Speech.SlangCodegen.FramingCursor` — capture-buffer framing policy

CHI-101 Phase A. Encodes the cursor arithmetic that
`SpeechProcessor::_mix_audio` runs every audio-thread tick: given
the carry-over from the previous tick and the count of newly
resampled samples this tick, how many full 480-sample (10 ms) frames
can be emitted, and how many samples are left over to carry forward?

## Policy (math)

```
total          = carry + newSamples
framesEmitted  = total / frameSize       // integer division
nextCarry      = total mod frameSize     // 0 ≤ nextCarry < frameSize
```

That's it. Both quantities are exact integer arithmetic — no
floating point, no thresholds, no hysteresis. The invariant is:

  carry · 1 + newSamples · 1  =  framesEmitted · frameSize  +  nextCarry

and `0 ≤ nextCarry < frameSize`.

## Why this kernel exists

`speech_processor.cpp:130` (as of this writing) implements the same
policy with a different formulation:

```cpp
while (capture_real_array_offset
       < resampled_frame_count - SPEECH_SETTING_BUFFER_FRAME_COUNT) {
  // emit a 480-frame packet
  capture_real_array_offset += SPEECH_SETTING_BUFFER_FRAME_COUNT;
}
```

That subtraction is `uint32_t`. If `resampled_frame_count < 480`
(degenerate first-tick or resampler underrun), the right-hand side
underflows to ~4 G and the loop reads past the buffer end. Even in
non-degenerate cases, the strict `<` mis-fires the boundary at
`resampled_frame_count == capture_real_array_offset + 480`.

The Lean policy above is immune to both: integer division can't
underflow, and the boundary is `total / frameSize` which gives 1
for `total == frameSize` exactly. The bit-exact harness in
`tests/slang_validate/framing_cursor_test.cpp` runs both the kernel
and the literal `speech_processor.cpp` formulation over a fixture
that triggers the underflow — they disagree, kernel right.

## Shape

The C++ runs as a single-thread loop per audio-thread tick (with
state held in `capture_real_array_offset`). The kernel mirrors this:
single-thread dispatch (`[numthreads(1, 1, 1)]`) that walks
`numTicks` simulated ticks in order, carrying state via `stateIO`.

For runtime use, dispatch with `numTicks = 1` per audio-thread tick.
For batch verification against recorded captures, dispatch with
`numTicks = N` in one shot.

## Bindings (set 0)

  0  ConstantBuffer<FramingCursorParams> {
       uint numTicks;
       uint frameSize;          // 480 for 10 ms @ 48 kHz
     }
  1  StructuredBuffer<uint>    newSamples       length = numTicks
  2  RWStructuredBuffer<uint>  framesEmitted    length = numTicks
  3  RWStructuredBuffer<uint>  stateIO          length = 1
       // [0] = carry samples in the buffer (0..frameSize - 1)

The kernel does NOT emit per-frame data — only the *count* of frames
that would be emitted per tick. The actual frame-data copy is a
parallel kernel (one thread per emitted frame) that can be added in
a follow-up; for the cursor-policy diff we only need the count and
the carry evolution.
-/

namespace Speech.SlangCodegen.FramingCursor

open LeanSlang

private def uintTy : SlangType := .scalar .uint

/-- Capture-buffer framing-cursor policy. Single-thread dispatch
    walks `numTicks` ticks in order; per tick, integer-divide
    `(carry + newSamples[i])` by `frameSize` to get
    `framesEmitted[i]`; the remainder becomes the next carry. -/
def shader : SlangShaderModule :=
  { structs :=
      [ { name := "FramingCursorParams"
        , fields :=
            [ ⟨"numTicks",  uintTy, Semantic.none, none, none, .qIn⟩
            , ⟨"frameSize", uintTy, Semantic.none, none, none, .qIn⟩ ] } ]
  , globals :=
      [ ⟨"params",        .const "FramingCursorParams", Semantic.none, some 0, some 0, .qIn⟩
      , ⟨"newSamples",    .roBuf uintTy,                Semantic.none, some 1, some 0, .qIn⟩
      , ⟨"framesEmitted", .rwBuf uintTy,                Semantic.none, some 2, some 0, .qIn⟩
      , ⟨"stateIO",       .rwBuf uintTy,                Semantic.none, some 3, some 0, .qIn⟩ ]
  , functions := [{
      attrs  := [.shaderCompute, .numthreads 1 1 1]
      name   := "main"
      params := [⟨"tid", .vec .uint 3, .svDispatchThreadId, none, none, .qIn⟩]
      body   :=
        [ .ifNoElse (.bin "!=" (.member (.var "tid") "x") (.litUint 0))
            [ .ret none ]
        , .declInit uintTy "carry" (.index (.var "stateIO") (.litUint 0))
        , .forCount "i" (.litUint 0) (.member (.var "params") "numTicks")
            [ .declInit uintTy "total"
                (.bin "+" (.var "carry") (.index (.var "newSamples") (.var "i")))
            , .declInit uintTy "k"
                (.bin "/" (.var "total") (.member (.var "params") "frameSize"))
            , .assign (.index (.var "framesEmitted") (.var "i")) (.var "k")
            , .assign (.var "carry")
                (.bin "%" (.var "total") (.member (.var "params") "frameSize"))
            ]
        , .assign (.index (.var "stateIO") (.litUint 0)) (.var "carry")
        ] }] }

/-- Pinned reference emission. -/
def expected : String :=
"struct FramingCursorParams {
  uint numTicks;
  uint frameSize;
};

[[vk::binding(0, 0)]]
ConstantBuffer<FramingCursorParams> params;
[[vk::binding(1, 0)]]
StructuredBuffer<uint> newSamples;
[[vk::binding(2, 0)]]
RWStructuredBuffer<uint> framesEmitted;
[[vk::binding(3, 0)]]
RWStructuredBuffer<uint> stateIO;

[shader(\"compute\")] [numthreads(1, 1, 1)]
void main(uint3 tid : SV_DispatchThreadID) {
  if ((tid.x != 0u)) {
    return;
  }
  uint carry = stateIO[0u];
  for (uint i = 0u; i < params.numTicks; ++i) {
    uint total = (carry + newSamples[i]);
    uint k = (total / params.frameSize);
    framesEmitted[i] = k;
    carry = (total % params.frameSize);
  }
  stateIO[0u] = carry;
}"

example : LeanSlang.emit shader = expected := by native_decide
example : shader.entryPointName = "main" := by native_decide

end Speech.SlangCodegen.FramingCursor
