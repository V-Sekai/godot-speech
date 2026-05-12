import LeanSlang

/-!
# `Speech.SlangCodegen.JitterAppend` — receive-side jitter-buffer evolution

CHI-101 Phase A. Encodes the per-packet decision logic that runs in
`Speech::on_received_audio_packet` (`speech.cpp:570+`): given the
current jitter-buffer state and an incoming packet's sequence ID,
decide

  * how many invalid filler packets to push (gap fill),
  * whether the new packet is a forward append (vs. an in-place
    update of an out-of-order slot),
  * which slot to update (for out-of-order),
  * how many front entries to drop to keep `size <= maxSize`.

## Why this kernel exists

The C++ code path uses a signed `int64_t` `sequence_id_offset` to
distinguish three sub-cases (forward / equal / out-of-order). The
arithmetic is subtle: slot indices for out-of-order packets are
computed as `bufferSize - 1 + offset` (a signed expression with
`offset ≤ 0`), and the kernel exists to pin that down formally
and catch any future drift the way `FramingCursor` caught F1.

## Policy

State carried across events via `stateIO`:

  [0] currentSeq      uint  — last seen sequence ID for this peer
  [1] currentSeqValid uint  — 0 if no packets seen yet, else 1
  [2] currentSize     uint  — number of entries in the jitter buffer

Per event with `incomingSeq[i]`:

```
if currentSeqValid == 0:
    # First packet from this peer: behave as if currentSeq = incomingSeq - 1
    # (matches speech.cpp:606-608). I.e. treat as forward-by-1.
    fillerCount[i]    = 0
    appendNew[i]      = 1
    slotIsValid[i]    = 0
    slotIdx[i]        = 0           # unused
    sizeAfterAppend   = currentSize + 1
    dropFromFront[i]  = max(0, sizeAfterAppend - maxSize)
    newSize           = sizeAfterAppend - dropFromFront[i]
    newCurrentSeq     = incomingSeq
    newCurrentSeqValid = 1

elif incomingSeq > currentSeq:                # forward
    offset            = incomingSeq - currentSeq
    fillerCount[i]    = offset - 1
    appendNew[i]      = 1
    slotIsValid[i]    = 0
    slotIdx[i]        = 0           # unused
    sizeAfterAppend   = currentSize + fillerCount[i] + 1
    dropFromFront[i]  = max(0, sizeAfterAppend - maxSize)
    newSize           = sizeAfterAppend - dropFromFront[i]
    newCurrentSeq     = incomingSeq

else:                                          # incomingSeq ≤ currentSeq (out-of-order / dup)
    # signed-form slot = currentSize - 1 + (incomingSeq - currentSeq)
    #                  = currentSize - 1 - (currentSeq - incomingSeq)
    diff              = currentSeq - incomingSeq      # ≥ 0
    fillerCount[i]    = 0
    appendNew[i]      = 0
    if currentSize >= 1 + diff:
        slotIsValid[i] = 1
        slotIdx[i]     = currentSize - 1 - diff
    else:                                      # too old to repair
        slotIsValid[i] = 0
        slotIdx[i]     = 0
    dropFromFront[i]  = 0
    newSize           = currentSize
    # currentSeq unchanged
```

The `else` branch matches the C++ for both `offset == 0` (duplicate)
and `offset < 0` (out-of-order). When `offset == 0` and the buffer is
non-empty, slot = `currentSize - 1` (the most-recent slot is
overwritten). When the buffer is empty, slot is "invalid" and the
duplicate is dropped silently. Both behaviors are faithful to
`speech.cpp:642-654`.

## Shape

Sequential per-event state machine. Single-thread dispatch
(`[numthreads(1, 1, 1)]`). Same idiom as `VadGate` and
`FramingCursor`.

## Bindings (set 0)

  0  ConstantBuffer<JitterAppendParams> { uint numEvents; uint maxSize; }
  1  StructuredBuffer<uint>    incomingSeq      length = numEvents
  2  RWStructuredBuffer<uint>  fillerCount      length = numEvents
  3  RWStructuredBuffer<uint>  appendNew        length = numEvents  (0 or 1)
  4  RWStructuredBuffer<uint>  slotIsValid      length = numEvents  (0 or 1)
  5  RWStructuredBuffer<uint>  slotIdx          length = numEvents
  6  RWStructuredBuffer<uint>  dropFromFront    length = numEvents
  7  RWStructuredBuffer<uint>  stateIO          length = 3
-/

namespace Speech.SlangCodegen.JitterAppend

open LeanSlang

private def uintTy : SlangType := .scalar .uint

/-- Per-event jitter-buffer evolution kernel. Sequential single-thread. -/
def shader : SlangShaderModule :=
  { structs :=
      [ { name := "JitterAppendParams"
        , fields :=
            [ ⟨"numEvents", uintTy, Semantic.none, none, none, .qIn⟩
            , ⟨"maxSize",   uintTy, Semantic.none, none, none, .qIn⟩ ] } ]
  , globals :=
      [ ⟨"params",        .const "JitterAppendParams", Semantic.none, some 0, some 0, .qIn⟩
      , ⟨"incomingSeq",   .roBuf uintTy,               Semantic.none, some 1, some 0, .qIn⟩
      , ⟨"fillerCount",   .rwBuf uintTy,               Semantic.none, some 2, some 0, .qIn⟩
      , ⟨"appendNew",     .rwBuf uintTy,               Semantic.none, some 3, some 0, .qIn⟩
      , ⟨"slotIsValid",   .rwBuf uintTy,               Semantic.none, some 4, some 0, .qIn⟩
      , ⟨"slotIdx",       .rwBuf uintTy,               Semantic.none, some 5, some 0, .qIn⟩
      , ⟨"dropFromFront", .rwBuf uintTy,               Semantic.none, some 6, some 0, .qIn⟩
      , ⟨"stateIO",       .rwBuf uintTy,               Semantic.none, some 7, some 0, .qIn⟩ ]
  , functions := [{
      attrs  := [.shaderCompute, .numthreads 1 1 1]
      name   := "main"
      params := [⟨"tid", .vec .uint 3, .svDispatchThreadId, none, none, .qIn⟩]
      body   :=
        [ .ifNoElse (.bin "!=" (.member (.var "tid") "x") (.litUint 0))
            [ .ret none ]
        , .declInit uintTy "currentSeq"      (.index (.var "stateIO") (.litUint 0))
        , .declInit uintTy "currentSeqValid" (.index (.var "stateIO") (.litUint 1))
        , .declInit uintTy "currentSize"     (.index (.var "stateIO") (.litUint 2))
        , .forCount "i" (.litUint 0) (.member (.var "params") "numEvents")
            [ .declInit uintTy "seq" (.index (.var "incomingSeq") (.var "i"))

            -- branch 1: first-packet path  (currentSeqValid == 0)
            , .ifThen (.bin "==" (.var "currentSeqValid") (.litUint 0))
                [ .assign (.index (.var "fillerCount")   (.var "i")) (.litUint 0)
                , .assign (.index (.var "appendNew")     (.var "i")) (.litUint 1)
                , .assign (.index (.var "slotIsValid")   (.var "i")) (.litUint 0)
                , .assign (.index (.var "slotIdx")       (.var "i")) (.litUint 0)
                , .declInit uintTy "sizeAfter"
                    (.bin "+" (.var "currentSize") (.litUint 1))
                , .declInit uintTy "drop"
                    (.ternary (.bin ">" (.var "sizeAfter") (.member (.var "params") "maxSize"))
                              (.bin "-" (.var "sizeAfter") (.member (.var "params") "maxSize"))
                              (.litUint 0))
                , .assign (.index (.var "dropFromFront") (.var "i")) (.var "drop")
                , .assign (.var "currentSize")      (.bin "-" (.var "sizeAfter") (.var "drop"))
                , .assign (.var "currentSeq")       (.var "seq")
                , .assign (.var "currentSeqValid")  (.litUint 1)
                ]

                -- else: currentSeqValid == 1
                [ .ifThen (.bin ">" (.var "seq") (.var "currentSeq"))
                    -- branch 2: forward (seq > currentSeq)
                    [ .declInit uintTy "offset"
                        (.bin "-" (.var "seq") (.var "currentSeq"))
                    , .declInit uintTy "filler"
                        (.bin "-" (.var "offset") (.litUint 1))
                    , .assign (.index (.var "fillerCount") (.var "i")) (.var "filler")
                    , .assign (.index (.var "appendNew")   (.var "i")) (.litUint 1)
                    , .assign (.index (.var "slotIsValid") (.var "i")) (.litUint 0)
                    , .assign (.index (.var "slotIdx")     (.var "i")) (.litUint 0)
                    , .declInit uintTy "sizeAfter"
                        (.bin "+" (.bin "+" (.var "currentSize") (.var "filler")) (.litUint 1))
                    , .declInit uintTy "drop"
                        (.ternary (.bin ">" (.var "sizeAfter") (.member (.var "params") "maxSize"))
                                  (.bin "-" (.var "sizeAfter") (.member (.var "params") "maxSize"))
                                  (.litUint 0))
                    , .assign (.index (.var "dropFromFront") (.var "i")) (.var "drop")
                    , .assign (.var "currentSize") (.bin "-" (.var "sizeAfter") (.var "drop"))
                    , .assign (.var "currentSeq")  (.var "seq")
                    ]
                    -- branch 3: out-of-order or duplicate (seq <= currentSeq)
                    [ .declInit uintTy "diff"
                        (.bin "-" (.var "currentSeq") (.var "seq"))
                    , .assign (.index (.var "fillerCount")   (.var "i")) (.litUint 0)
                    , .assign (.index (.var "appendNew")     (.var "i")) (.litUint 0)
                    , .assign (.index (.var "dropFromFront") (.var "i")) (.litUint 0)
                    , .ifThen
                        (.bin ">=" (.var "currentSize")
                                   (.bin "+" (.litUint 1) (.var "diff")))
                        [ .assign (.index (.var "slotIsValid") (.var "i")) (.litUint 1)
                        , .assign (.index (.var "slotIdx") (.var "i"))
                            (.bin "-" (.bin "-" (.var "currentSize") (.litUint 1))
                                      (.var "diff"))
                        ]
                        [ .assign (.index (.var "slotIsValid") (.var "i")) (.litUint 0)
                        , .assign (.index (.var "slotIdx")     (.var "i")) (.litUint 0)
                        ]
                    ]
                ]
            ]
        , .assign (.index (.var "stateIO") (.litUint 0)) (.var "currentSeq")
        , .assign (.index (.var "stateIO") (.litUint 1)) (.var "currentSeqValid")
        , .assign (.index (.var "stateIO") (.litUint 2)) (.var "currentSize")
        ] }] }

/-- Pinned reference emission. Drift in `LeanSlang.Emit` that affects
    this output trips `native_decide` below. Regenerate after a deliberate
    change via `lake exe emit_shaders /tmp/emit && cat /tmp/emit/jitter_append.slang`. -/
def expected : String :=
"struct JitterAppendParams {
  uint numEvents;
  uint maxSize;
};

[[vk::binding(0, 0)]]
ConstantBuffer<JitterAppendParams> params;
[[vk::binding(1, 0)]]
StructuredBuffer<uint> incomingSeq;
[[vk::binding(2, 0)]]
RWStructuredBuffer<uint> fillerCount;
[[vk::binding(3, 0)]]
RWStructuredBuffer<uint> appendNew;
[[vk::binding(4, 0)]]
RWStructuredBuffer<uint> slotIsValid;
[[vk::binding(5, 0)]]
RWStructuredBuffer<uint> slotIdx;
[[vk::binding(6, 0)]]
RWStructuredBuffer<uint> dropFromFront;
[[vk::binding(7, 0)]]
RWStructuredBuffer<uint> stateIO;

[shader(\"compute\")] [numthreads(1, 1, 1)]
void main(uint3 tid : SV_DispatchThreadID) {
  if ((tid.x != 0u)) {
    return;
  }
  uint currentSeq = stateIO[0u];
  uint currentSeqValid = stateIO[1u];
  uint currentSize = stateIO[2u];
  for (uint i = 0u; i < params.numEvents; ++i) {
    uint seq = incomingSeq[i];
    if ((currentSeqValid == 0u)) {
      fillerCount[i] = 0u;
      appendNew[i] = 1u;
      slotIsValid[i] = 0u;
      slotIdx[i] = 0u;
      uint sizeAfter = (currentSize + 1u);
      uint drop = ((sizeAfter > params.maxSize) ? (sizeAfter - params.maxSize) : 0u);
      dropFromFront[i] = drop;
      currentSize = (sizeAfter - drop);
      currentSeq = seq;
      currentSeqValid = 1u;
    } else {
      if ((seq > currentSeq)) {
        uint offset = (seq - currentSeq);
        uint filler = (offset - 1u);
        fillerCount[i] = filler;
        appendNew[i] = 1u;
        slotIsValid[i] = 0u;
        slotIdx[i] = 0u;
        uint sizeAfter = ((currentSize + filler) + 1u);
        uint drop = ((sizeAfter > params.maxSize) ? (sizeAfter - params.maxSize) : 0u);
        dropFromFront[i] = drop;
        currentSize = (sizeAfter - drop);
        currentSeq = seq;
      } else {
        uint diff = (currentSeq - seq);
        fillerCount[i] = 0u;
        appendNew[i] = 0u;
        dropFromFront[i] = 0u;
        if ((currentSize >= (1u + diff))) {
          slotIsValid[i] = 1u;
          slotIdx[i] = ((currentSize - 1u) - diff);
        } else {
          slotIsValid[i] = 0u;
          slotIdx[i] = 0u;
        }
      }
    }
  }
  stateIO[0u] = currentSeq;
  stateIO[1u] = currentSeqValid;
  stateIO[2u] = currentSize;
}"

/-- The pretty-printer matches the pinned reference. -/
example : LeanSlang.emit shader = expected := by native_decide

/-- The kernel's entry-point name is `main`. -/
example : shader.entryPointName = "main" := by native_decide

end Speech.SlangCodegen.JitterAppend
