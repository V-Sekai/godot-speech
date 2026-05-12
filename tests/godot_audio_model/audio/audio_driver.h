// CHI-101 Phase A pass-3 — `AudioDriver` base class stand-in.
//
// Mirrors the subset of `servers/audio/audio_server.h::AudioDriver`
// that godot-speech and the CoreAudio driver actually use. Sample
// playback (Apple/Web/Steam Audio extensions) is not modeled — the
// pure-virtual `add_sample_*` methods on the engine's class are
// omitted here since CoreAudio's driver only overrides the
// init/start/finish/lock/mix_rate/input_device path.

#pragma once

#include "../core/core_types.h"

class AudioDriver {
	static AudioDriver *singleton;

protected:
	Vector<int32_t> input_buffer;
	unsigned int input_position = 0;
	unsigned int input_size = 0;

	// In the engine `audio_server_process` pumps mixed frames from the
	// `AudioServer` bus tree into the output buffer. The test binary
	// doesn't model the bus tree (only the capture path), so this is
	// a no-op that fills with silence — enough to keep the CoreAudio
	// output-callback contract intact while we focus on the input
	// path used by SpeechProcessor.
	void audio_server_process(int p_frames, int32_t *p_buffer,
			bool /*p_update_mix_time*/ = true) {
		if (p_buffer == nullptr) {
			return;
		}
		for (int i = 0; i < p_frames * 2; ++i) {
			p_buffer[i] = 0;
		}
	}

	void update_mix_time(int /*p_frames*/) {}

	void start_counting_ticks() {}
	void stop_counting_ticks() {}

	void input_buffer_init(int driver_buffer_frames) {
		input_buffer.resize(driver_buffer_frames * 4); // stereo + headroom
		input_position = 0;
		input_size = 0;
	}

	void input_buffer_write(int32_t sample) {
		if (input_buffer.is_empty()) {
			return;
		}
		input_buffer[static_cast<int>(input_position)] = sample;
		input_position = (input_position + 1) % static_cast<unsigned int>(input_buffer.size());
		if (input_size < static_cast<unsigned int>(input_buffer.size())) {
			++input_size;
		}
	}

public:
	enum SpeakerMode {
		SPEAKER_MODE_STEREO,
		SPEAKER_SURROUND_31,
		SPEAKER_SURROUND_51,
		SPEAKER_SURROUND_71,
	};

	static AudioDriver *get_singleton() { return singleton; }
	void set_singleton() { singleton = this; }

	virtual const char *get_name() const = 0;

	virtual Error init() = 0;
	virtual void start() = 0;
	virtual int get_mix_rate() const = 0;
	virtual int get_input_mix_rate() const { return get_mix_rate(); }
	virtual SpeakerMode get_speaker_mode() const = 0;
	virtual float get_latency() { return 0; }

	virtual void lock() = 0;
	virtual void unlock() = 0;
	virtual void finish() = 0;

	virtual PackedStringArray get_output_device_list() {
		PackedStringArray a;
		a.push_back(String("Default"));
		return a;
	}
	virtual String get_output_device() { return String("Default"); }
	virtual void set_output_device(const String & /*p_name*/) {}

	virtual Error input_start() { return FAILED; }
	virtual Error input_stop() { return FAILED; }

	virtual PackedStringArray get_input_device_list() {
		PackedStringArray a;
		a.push_back(String("Default"));
		return a;
	}
	virtual String get_input_device() { return String("Default"); }
	virtual void set_input_device(const String & /*p_name*/) {}

	Vector<int32_t> get_input_buffer() { return input_buffer; }
	unsigned int get_input_position() { return input_position; }
	unsigned int get_input_size() { return input_size; }

	// Channel-count to speaker-mode helper. Matches the engine's
	// behavior: 2-ch stereo, 4-ch quad reported as 3.1, 6-ch as 5.1,
	// 8-ch as 7.1, anything else collapses to stereo.
	SpeakerMode get_speaker_mode_by_total_channels(int p_channels) const {
		switch (p_channels) {
			case 4:
				return SPEAKER_SURROUND_31;
			case 6:
				return SPEAKER_SURROUND_51;
			case 8:
				return SPEAKER_SURROUND_71;
			default:
				return SPEAKER_MODE_STEREO;
		}
	}
	int get_total_channels_by_speaker_mode(SpeakerMode p_mode) const {
		switch (p_mode) {
			case SPEAKER_SURROUND_31:
				return 4;
			case SPEAKER_SURROUND_51:
				return 6;
			case SPEAKER_SURROUND_71:
				return 8;
			case SPEAKER_MODE_STEREO:
			default:
				return 2;
		}
	}

	// The engine's `_get_configured_mix_rate` reads from ProjectSettings.
	// The test binary has no project settings, so we hard-code a sensible
	// default. Callers that need a different rate set it via the driver's
	// own ctor / init path instead.
	int _get_configured_mix_rate() { return 48000; }

	AudioDriver() = default;
	virtual ~AudioDriver() = default;
};
