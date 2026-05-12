// CHI-101 Phase A pass-4 — minimal `AudioEffect` + `AudioEffectInstance`
// base classes.
//
// The reference engine routes every audio bus through a chain of
// `AudioEffect` resources; each effect spawns an `AudioEffectInstance`
// that runs on the audio thread. godot-speech only ever interacts
// with the *capture* effect (AudioEffectCapture), and only via its
// reader API — `get_buffer`, `get_frames_available`,
// `get_buffer_length_frames`. The `instantiate()` / `process()`
// virtuals never fire in the test path (the engine's audio thread
// isn't running), so they're declared here as no-op defaults.

#pragma once

#include "../core/audio_frame.h"
#include "../core/core_types.h"

class AudioEffectInstance : public RefCounted {
public:
	virtual void process(const AudioFrame * /*src*/, AudioFrame * /*dst*/,
			int /*p_frame_count*/) {}
	virtual bool process_silence() const { return false; }
};

class AudioEffect : public RefCounted {
public:
	virtual Ref<AudioEffectInstance> instantiate() {
		return Ref<AudioEffectInstance>();
	}
};
