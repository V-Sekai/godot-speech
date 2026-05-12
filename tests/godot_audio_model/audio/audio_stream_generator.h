// CHI-101 Phase A pass-5 — `AudioStreamGenerator` +
// `AudioStreamGeneratorPlayback` stand-ins.
//
// godot-speech uses this as the speaker-side ring on the receive
// path: every incoming voice packet decoded by
// `Speech::attempt_to_feed_stream` is `push_buffer`-ed into the
// playback; the engine's audio thread normally drains the ring at
// `mix_rate`. The test binary has no audio thread, so a test-only
// `consume_test_frames(int)` simulates the speaker draining.
//
// Engine reader-API godot-speech depends on:
//
//   AudioStreamGenerator::set_mix_rate(float) / get_mix_rate() -> float
//   AudioStreamGenerator::set_buffer_length(float) / get_buffer_length() -> float
//   AudioStreamGeneratorPlayback::push_buffer(PackedVector2Array) -> bool
//   AudioStreamGeneratorPlayback::get_frames_available() -> int   (FREE space)
//   AudioStreamGeneratorPlayback::get_skips() -> int              (overflow count)
//
// Note on get_frames_available semantics: the engine returns
// available *write* room (frames that can still be pushed before
// the ring overflows), NOT data-currently-in-buffer. godot-speech
// uses it as `to_fill` in attempt_to_feed_stream, computing
// `required_packets = floor(to_fill / 480)`. Matching that
// convention is required for the runtime to agree with the
// JitterAppend/FramingCursor kernels' expectations.

#pragma once

#include "../core/audio_frame.h"
#include "../core/core_types.h"
#include "../core/ring_buffer.h"
#include "audio_stream.h"

class AudioStreamGenerator;

class AudioStreamGeneratorPlayback : public AudioStreamPlayback {
	friend class AudioStreamGenerator;

	RingBuffer<AudioFrame> buffer;
	SafeNumeric<int> skips;
	int total_capacity = 0;
	bool buffer_initialized = false;

	void initialize(float mix_rate, float buffer_length_seconds) {
		const int want_frames = static_cast<int>(mix_rate * buffer_length_seconds);
		int power = 1;
		while ((1 << power) < want_frames) {
			++power;
		}
		buffer.resize(power);
		total_capacity = buffer.size();
		buffer_initialized = true;
	}

public:
	int get_frames_available() const {
		// Engine convention: free *write* room.
		return buffer.space_left();
	}

	int get_skips() const { return skips.get(); }

	bool push_buffer(const PackedVector2Array &frames) {
		const int n = frames.size();
		if (n == 0) {
			return true;
		}
		if (buffer.space_left() < n) {
			skips.increment();
			return false;
		}
		const AudioFrame *src = reinterpret_cast<const AudioFrame *>(frames.ptr());
		const int wrote = buffer.write(src, n);
		return wrote == n;
	}

	// Engine has `can_push_buffer(int)` on the playback in some
	// branches; mirror it for completeness.
	bool can_push_buffer(int frames) const {
		return buffer.space_left() >= frames;
	}

	int get_total_capacity() const { return total_capacity; }

	// === Test-only API ========================================
	// Simulates the speaker draining N frames. Returns frames
	// actually consumed (less than N if the ring runs dry — the
	// engine's audio thread would silence-fill in this case).
	int consume_test_frames(int n) {
		AudioFrame discard[256];
		int consumed = 0;
		while (consumed < n) {
			const int chunk = ((n - consumed) < 256) ? (n - consumed) : 256;
			const int got = buffer.read(discard, chunk);
			if (got == 0) {
				break;
			}
			consumed += got;
		}
		return consumed;
	}

	// Drain into a caller-supplied output for inspection.
	PackedVector2Array consume_test_frames_into(int n) {
		PackedVector2Array out;
		out.resize(n);
		AudioFrame *dst = reinterpret_cast<AudioFrame *>(out.ptrw());
		const int got = buffer.read(dst, n);
		out.resize(got);
		return out;
	}
};

class AudioStreamGenerator : public AudioStream {
	float mix_rate = 44100.0f;
	float buffer_length_seconds = 0.5f;

public:
	void set_mix_rate(float p_rate) { mix_rate = p_rate; }
	float get_mix_rate() const { return mix_rate; }

	void set_buffer_length(float p_seconds) {
		buffer_length_seconds = p_seconds;
	}
	float get_buffer_length() const { return buffer_length_seconds; }

	// Engine API: `instantiate_playback()` constructs the playback.
	Ref<AudioStreamGeneratorPlayback> instantiate_playback() {
		Ref<AudioStreamGeneratorPlayback> pb = new AudioStreamGeneratorPlayback();
		pb->initialize(mix_rate, buffer_length_seconds);
		return pb;
	}
};
