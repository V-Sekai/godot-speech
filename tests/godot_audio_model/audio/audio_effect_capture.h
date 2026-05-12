// CHI-101 Phase A pass-4 — `AudioEffectCapture` stand-in.
//
// godot-speech consumes this via three methods only:
//
//   PackedVector2Array AudioEffectCapture::get_buffer(int len);
//   int AudioEffectCapture::get_buffer_length_frames();
//   int AudioEffectCapture::get_frames_available();
//
// In the engine, `process()` runs on the audio thread and writes
// captured mic frames into the ring; the consumer thread pops via
// `get_buffer`. The test binary has no audio thread, so we add a
// **test-only** non-engine method `push_test_frames` that the test
// driver calls to feed synthetic mic audio. Production-path
// consumers see the same `get_buffer` API as in the engine.
//
// Bridges between `AudioFrame` (the engine's stereo float pair) and
// `Vector2` (which `PackedVector2Array::get_buffer` returns): same
// memory layout, so we use `reinterpret_cast` rather than a copy
// at the ring boundary.

#pragma once

#include "../core/core_types.h"
#include "../core/ring_buffer.h"

#include "audio_effect.h"

class AudioEffectCapture : public AudioEffect {
	RingBuffer<AudioFrame> buffer;
	SafeNumeric<uint64_t> discarded_frames;
	SafeNumeric<uint64_t> pushed_frames;
	float buffer_length_seconds = 0.1f;
	uint32_t mix_rate = 48000;
	bool buffer_initialized = false;

	void ensure_initialized() {
		if (buffer_initialized) {
			return;
		}
		const uint32_t want_frames = static_cast<uint32_t>(
				static_cast<double>(mix_rate) * buffer_length_seconds);
		// Pick the smallest power-of-two ≥ want_frames; engine does
		// the same via Math::next_power_of_2.
		int power = 1;
		while ((1u << power) < want_frames) {
			++power;
		}
		buffer.resize(power);
		buffer_initialized = true;
	}

public:
	AudioEffectCapture() = default;

	// Engine-equivalent setters (`set_buffer_length` in seconds;
	// engine reads input mix rate from AudioServer, we accept it
	// directly so the test driver can vary it without touching the
	// AudioServer model).
	void set_buffer_length(float p_buffer_length_seconds) {
		buffer_length_seconds = p_buffer_length_seconds;
		buffer_initialized = false;
	}
	float get_buffer_length() const { return buffer_length_seconds; }

	void set_mix_rate(uint32_t p_mix_rate) {
		mix_rate = p_mix_rate;
		buffer_initialized = false;
	}
	uint32_t get_mix_rate() const { return mix_rate; }

	int get_buffer_length_frames() {
		ensure_initialized();
		return buffer.size();
	}

	int get_frames_available() {
		ensure_initialized();
		return buffer.data_left();
	}

	bool can_get_buffer(int p_frames) {
		return get_frames_available() >= p_frames;
	}

	int64_t get_discarded_frames() const {
		return static_cast<int64_t>(discarded_frames.get());
	}
	int64_t get_pushed_frames() const {
		return static_cast<int64_t>(pushed_frames.get());
	}

	void clear_buffer() {
		ensure_initialized();
		buffer.clear();
		discarded_frames.set(0);
		pushed_frames.set(0);
	}

	// Engine API: pop `p_len` AudioFrames as Vector2 (.x = left, .y = right).
	// Returns fewer frames if the ring has less; size of the returned array
	// equals what was actually read.
	PackedVector2Array get_buffer(int p_len) {
		ensure_initialized();
		const int avail = buffer.data_left();
		const int n = (p_len < avail) ? p_len : avail;
		PackedVector2Array out;
		out.resize(n);
		if (n > 0) {
			AudioFrame *dst = reinterpret_cast<AudioFrame *>(out.ptrw());
			buffer.read(dst, n);
		}
		return out;
	}

	// === Test-only API (not in the engine) ====================
	// The test driver pushes synthetic mic audio via this entry
	// point. On the engine side, `process()` does the equivalent
	// write but with frames from the audio thread. Matching the
	// engine's overflow behavior: drop oldest, bump
	// discarded_frames.
	void push_test_frames(const Vector2 *frames, int p_n) {
		ensure_initialized();
		const AudioFrame *src = reinterpret_cast<const AudioFrame *>(frames);
		int remaining = p_n;
		while (remaining > 0) {
			const int wrote = buffer.write(src, remaining);
			pushed_frames.add(static_cast<uint64_t>(wrote));
			remaining -= wrote;
			src += wrote;
			if (remaining > 0) {
				// Ring full; drop one frame from the front and retry.
				AudioFrame discarded;
				buffer.read(&discarded, 1);
				discarded_frames.add(1);
			}
		}
	}

	void push_test_frames(const PackedVector2Array &frames) {
		push_test_frames(frames.ptr(), frames.size());
	}

	virtual Ref<AudioEffectInstance> instantiate() override {
		// Capture's instance just writes into our ring; no real
		// instance needed for the test path. Return null to make
		// it obvious if the audio thread ever tries to use it.
		return Ref<AudioEffectInstance>();
	}
};
