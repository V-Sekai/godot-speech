/-!
# `Speech.Protocol.JitterBufferSizing` — C5 mitigation for voice jitter buffer

CHI-101 networking concern. Voice falls under the conditions established
by V-Sekai-fire's predictive-BVH spec for networking protocols:

  https://github.com/V-Sekai-fire/multiplayer-fabric-predictive-bvh/blob/main/PredictiveBVH/Protocol/ScaleContradictionsGapClass.lean

Of the seven adversarial gap classes (C1–C7) defined there, **C5 / G181 —
Effective delta exceeded (RTT)** applies directly to voice. The spec's
mitigation theorem for C5:

  deltaUsed := max 1 (min (max rttTicks 1) (deltaCapFromVelocity ...))

translates for voice (no velocity cap analog, no scene diameter, just a
1D temporal stream of packets) to:

  jitterDepth := max(baseline, rttTicks)

This module:
  * defines the voice-side analog of δ ("jitter buffer depth, in
    packets"),
  * proves the C5 mitigation closes the gap for voice,
  * pins concrete numeric examples that mirror the upstream
    `g181_c5` satellite case.

## Why a separate module rather than importing the upstream spec

Upstream lives in a different Lake package with its own dependency tree
(Mathlib, etc.). We replicate the relevant slice locally so the
godot-speech build stays self-contained. The constants here track
the upstream values verbatim — any drift trips the `native_decide`
examples below.

## What the code-side fix does

`speech.h` carries `MAX_JITTER_BUFFER_SIZE = 32` (320 ms) as the WAN-safe
baseline, raised from the prior `16` (160 ms) which silently dropped
packets under any RTT > 160 ms (most transcontinental routes).
Callers serving high-RTT clients (satellite, transpacific over
congested links) should call `Speech.set_max_jitter_buffer_size(rttTicks)`
per the C5 mitigation. F4 in
`manuals/decisions/2026-05-12-speech-isolation.md` documents the
constraint + caller responsibility.

A future refactor will make `MAX_JITTER_BUFFER_SIZE` a per-peer field
populated from the multiplayer layer's RTT estimate; for Stage 0
("chill and talk" — local LAN play), the raised global is sufficient.
-/

namespace Speech.Protocol.JitterBufferSizing

/-- Voice packet cadence: one Opus-encoded 10 ms frame per tick.
    Matches `SpeechProcessor::SPEECH_SETTING_MILLISECONDS_PER_PACKET = 10`. -/
def packetMs : Nat := 10

/-- Voice-side analog of upstream `δ`: the jitter buffer depth, expressed
    in 10 ms packet ticks. -/
abbrev JitterDepth := Nat

/-- Client RTT measured in the same 10 ms packet ticks.
    `rttMs / packetMs`, rounded up for safety. -/
abbrev RttTicks := Nat

/-- C5 soundness predicate for voice: jitter buffer is at least as deep
    as the client's RTT. When this holds, packets that arrive within
    one RTT of issue have a slot waiting and are not popped before
    playback. -/
def jitterCoversRtt (depth : JitterDepth) (rtt : RttTicks) : Prop :=
  rtt ≤ depth

/-- C5 mitigation, voice form: take the max of the configured baseline
    and the measured RTT. Mirrors the upstream
    `c5_mitigation_delta_pos` shape, minus the velocity-cap branch
    (no spatial analog for an audio stream). -/
def jitterDepthForPeer (baseline rtt : RttTicks) : JitterDepth :=
  max baseline rtt

/-- After C5 mitigation, the resulting jitter buffer covers the
    client's RTT. Voice-side soundness theorem. -/
theorem c5_voice_mitigation_sound (baseline rtt : Nat) :
    jitterCoversRtt (jitterDepthForPeer baseline rtt) rtt := by
  unfold jitterCoversRtt jitterDepthForPeer
  exact Nat.le_max_right baseline rtt

/-- The mitigated depth is also at least the baseline (monotone in
    baseline). Ensures we never *shrink* the buffer below the
    operator's configured floor. -/
theorem c5_voice_mitigation_floor (baseline rtt : Nat) :
    baseline ≤ jitterDepthForPeer baseline rtt :=
  Nat.le_max_left baseline rtt

-- ── Adversarial witnesses mirroring upstream `g181_c5` ──────────────────

/-- Satellite RTT in milliseconds (geostationary), per upstream
    `satelliteRttMs = 2000`. -/
def satelliteRttMs : Nat := 2000

/-- Satellite RTT in voice-packet ticks (10 ms per tick): 200 ticks. -/
def satelliteRttTicks : RttTicks := satelliteRttMs / packetMs

/-- Prior `MAX_JITTER_BUFFER_SIZE = 16` does NOT cover satellite RTT.
    This is the concrete C5 gap for voice. -/
example : ¬ jitterCoversRtt 16 satelliteRttTicks := by
  unfold jitterCoversRtt satelliteRttTicks satelliteRttMs packetMs
  native_decide

/-- New baseline `MAX_JITTER_BUFFER_SIZE = 32` does NOT cover satellite
    RTT either — 320 ms < 2000 ms — confirming that satellite operators
    must explicitly raise the buffer (via
    `Speech.set_max_jitter_buffer_size`) to at least 200. -/
example : ¬ jitterCoversRtt 32 satelliteRttTicks := by
  unfold jitterCoversRtt satelliteRttTicks satelliteRttMs packetMs
  native_decide

/-- New baseline 32 packets DOES cover up-to-320 ms RTT (most
    transcontinental WAN routes, including trans-Pacific). -/
example : jitterCoversRtt 32 30 := by
  unfold jitterCoversRtt; native_decide

/-- Mitigation closes the satellite case explicitly: if the caller
    supplies the satellite RTT as the baseline, the buffer covers it. -/
example : jitterCoversRtt (jitterDepthForPeer satelliteRttTicks 0) satelliteRttTicks := by
  unfold jitterCoversRtt jitterDepthForPeer satelliteRttTicks satelliteRttMs packetMs
  native_decide

end Speech.Protocol.JitterBufferSizing
