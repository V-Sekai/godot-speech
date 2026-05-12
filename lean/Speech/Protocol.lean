import Speech.Protocol.JitterBufferSizing

/-!
# `Speech.Protocol` — networking-protocol invariants for the voice path

CHI-101 networking concerns formalized under the conditions established
by V-Sekai-fire's predictive-BVH spec
(`PredictiveBVH/Protocol/ScaleContradictionsGapClass.lean`).

Submodules carry voice-specific applications of the upstream gap
classes:

  * `Speech.Protocol.JitterBufferSizing` — applies C5 (G181, "effective
    delta exceeded by RTT") to the receive-side jitter buffer.

C1/C2/C3/C6/C7 are spatial / motion concerns that don't apply directly
to a 1D temporal audio stream. C4 (entity lifecycle) is partially
covered by the first-packet path in `on_received_audio_packet` and is
a candidate for further formalization in a follow-up.
-/
