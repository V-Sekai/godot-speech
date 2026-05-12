import LeanSlang

/-!
# `Speech.SlangCodegen.VadGate` — hysteresis state machine for VAD output

CHI-101 Phase A. Turns per-frame voice-activity scores (from the
Silero classifier in godot-whisper, or any equivalent producer) into
a gated boolean stream suitable for driving Opus-encode + WebTransport
send.

The classifier itself is upstream-pinned (libopus and Silero ML
weights are not in scope). The gating *policy* on top — what counts
as a voice frame given a score history — is what gets formalized here.

## Policy

Per-frame state machine, processed in order:

  enterThreshold ∈ (0, 1)   e.g. 0.6 — score must exceed this to trip "on"
  exitThreshold  ∈ (0, 1)   e.g. 0.4 — score must stay above this to keep alive
  hangoverFrames ∈ ℕ        e.g. 8   — frames to hold "on" after score dips
                                       (~80 ms at 10 ms/frame; prevents
                                       chopping off final consonants)

Hysteresis: `enterThreshold > exitThreshold` so transient dips don't
flap the gate. Hangover prevents tail-cutoff at end of word.

```
state: { active: bool, hangover: uint }   // initially {false, 0}

per frame i with score[i]:
  if !active:
    if score[i] > enterThreshold:
      active   = true
      hangover = hangoverFrames
  else:  // active
    if score[i] > exitThreshold:
      hangover = hangoverFrames           // refresh
    elif hangover > 0:
      hangover = hangover - 1
    else:
      active = false

  gated[i] = active
```

## Shape

Inherently sequential — each frame's decision depends on the previous
frame's state. The kernel runs as a single-thread dispatch
(`[numthreads(1, 1, 1)]`, one group) that walks `numFrames` in order.
This is consistent with how cloth_dynamics handles small-scale
sequential logic (`dot_reduce_serial`).

For the godot-speech audio thread we'd dispatch this once per
captured batch of frames (typically 1 frame at 100 Hz, but the
kernel handles N≥1 uniformly so it's drop-in for batched offline
verification against recorded clips too).

## Bindings (set 0)

  0  ConstantBuffer<VadGateParams> {
       uint numFrames;
       float enterThreshold;
       float exitThreshold;
       uint hangoverFrames;
     }
  1  StructuredBuffer<float>   score      length = numFrames
  2  RWStructuredBuffer<uint>  gated      length = numFrames    (0 or 1)
  3  RWStructuredBuffer<uint>  stateIO    length = 2
       // [0] = active (0 or 1) carried across dispatches
       // [1] = hangover remaining (0..hangoverFrames)

State carry-over via `stateIO` lets the gate run across arbitrarily
many small dispatches without resetting at every audio-thread tick.

The pinned reference text below is asserted by `native_decide`. To
regenerate after a deliberate change, run
`lake exe emit_shaders /tmp/emit && cat /tmp/emit/vad_gate.slang`
and replace `expected` with the new output.
-/

namespace Speech.SlangCodegen.VadGate

open LeanSlang

private def floatTy : SlangType := .scalar .float
private def uintTy  : SlangType := .scalar .uint

/-- VAD gate hysteresis kernel. Single-thread dispatch walks frames
    in order so the per-frame state dependency is honored without an
    inter-thread sync. -/
def shader : SlangShaderModule :=
  { structs :=
      [ { name := "VadGateParams"
        , fields :=
            [ ⟨"numFrames",      uintTy,  Semantic.none, none, none, .qIn⟩
            , ⟨"enterThreshold", floatTy, Semantic.none, none, none, .qIn⟩
            , ⟨"exitThreshold",  floatTy, Semantic.none, none, none, .qIn⟩
            , ⟨"hangoverFrames", uintTy,  Semantic.none, none, none, .qIn⟩ ] } ]
  , globals :=
      [ ⟨"params",  .const "VadGateParams",  Semantic.none, some 0, some 0, .qIn⟩
      , ⟨"score",   .roBuf floatTy,          Semantic.none, some 1, some 0, .qIn⟩
      , ⟨"gated",   .rwBuf uintTy,           Semantic.none, some 2, some 0, .qIn⟩
      , ⟨"stateIO", .rwBuf uintTy,           Semantic.none, some 3, some 0, .qIn⟩ ]
  , functions := [{
      attrs  := [.shaderCompute, .numthreads 1 1 1]
      name   := "main"
      params := [⟨"tid", .vec .uint 3, .svDispatchThreadId, none, none, .qIn⟩]
      body   :=
        [ .ifNoElse (.bin "!=" (.member (.var "tid") "x") (.litUint 0))
            [ .ret none ]
        , .declInit uintTy "active"   (.index (.var "stateIO") (.litUint 0))
        , .declInit uintTy "hangover" (.index (.var "stateIO") (.litUint 1))
        , .forCount "i" (.litUint 0) (.member (.var "params") "numFrames")
            [ .declInit floatTy "s" (.index (.var "score") (.var "i"))
            , .ifThen (.bin "==" (.var "active") (.litUint 0))
                [ .ifNoElse
                    (.bin ">" (.var "s") (.member (.var "params") "enterThreshold"))
                    [ .assign (.var "active")   (.litUint 1)
                    , .assign (.var "hangover")
                        (.member (.var "params") "hangoverFrames") ]
                ]
                [ .ifThen
                    (.bin ">" (.var "s") (.member (.var "params") "exitThreshold"))
                    [ .assign (.var "hangover")
                        (.member (.var "params") "hangoverFrames") ]
                    [ .ifThen (.bin ">" (.var "hangover") (.litUint 0))
                        [ .assign (.var "hangover")
                            (.bin "-" (.var "hangover") (.litUint 1)) ]
                        [ .assign (.var "active") (.litUint 0) ]
                    ]
                ]
            , .assign (.index (.var "gated") (.var "i")) (.var "active")
            ]
        , .assign (.index (.var "stateIO") (.litUint 0)) (.var "active")
        , .assign (.index (.var "stateIO") (.litUint 1)) (.var "hangover")
        ] }] }

/-- Pinned reference emission. Drift in `LeanSlang.Emit` that affects
    this output trips `native_decide` below. Update both this string and
    the kernel in lockstep. -/
def expected : String :=
"struct VadGateParams {
  uint numFrames;
  float enterThreshold;
  float exitThreshold;
  uint hangoverFrames;
};

[[vk::binding(0, 0)]]
ConstantBuffer<VadGateParams> params;
[[vk::binding(1, 0)]]
StructuredBuffer<float> score;
[[vk::binding(2, 0)]]
RWStructuredBuffer<uint> gated;
[[vk::binding(3, 0)]]
RWStructuredBuffer<uint> stateIO;

[shader(\"compute\")] [numthreads(1, 1, 1)]
void main(uint3 tid : SV_DispatchThreadID) {
  if ((tid.x != 0u)) {
    return;
  }
  uint active = stateIO[0u];
  uint hangover = stateIO[1u];
  for (uint i = 0u; i < params.numFrames; ++i) {
    float s = score[i];
    if ((active == 0u)) {
      if ((s > params.enterThreshold)) {
        active = 1u;
        hangover = params.hangoverFrames;
      }
    } else {
      if ((s > params.exitThreshold)) {
        hangover = params.hangoverFrames;
      } else {
        if ((hangover > 0u)) {
          hangover = (hangover - 1u);
        } else {
          active = 0u;
        }
      }
    }
    gated[i] = active;
  }
  stateIO[0u] = active;
  stateIO[1u] = hangover;
}"

/-- The pretty-printer matches the pinned reference. -/
example : LeanSlang.emit shader = expected := by native_decide

/-- The kernel's entry-point name is `main`. -/
example : shader.entryPointName = "main" := by native_decide

end Speech.SlangCodegen.VadGate
