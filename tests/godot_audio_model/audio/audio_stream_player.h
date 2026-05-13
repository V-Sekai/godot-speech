// CHI-101 Phase A pass-6 — `AudioStreamPlayer` / `Player2D` /
// `Player3D` stand-ins.
//
// godot-speech treats the three players polymorphically — capture
// uses `AudioStreamPlayer` (always 2D-positionless), receive can
// be any of the three. The shared API godot-speech exercises is:
//
//   void set_stream(Ref<AudioStream>)
//   set_bus(StringName)
//   call("play", float position)
//   call("get_playback_position") -> float
//   call("get_stream_playback") -> Ref<AudioStreamGeneratorPlayback>
//   has_method(StringName) -> bool
//
// All three derived players share the same implementation; in the
// engine they differ by positional metadata that doesn't affect
// the audio path godot-speech walks. We share an
// `AudioStreamPlayerCommon` base and define Player / Player2D /
// Player3D as thin tags so `cast_to<>` succeeds for all three.

#pragma once

#include "../core/core_types.h"
#include "audio_stream_generator.h"
#include "node.h"

class AudioStreamPlayerCommon : public Node {
	Ref<AudioStream> stream;
	Ref<AudioStreamPlayback> playback;
	StringName bus_name = StringName("Master");
	float playback_position = 0.0f;
	bool playing = false;

public:
	void set_stream(const Ref<AudioStream> &p_stream) {
		stream = p_stream;
		// If the stream is a generator, instantiate its playback
		// eagerly so `get_stream_playback` returns a stable Ref.
		// Engine semantics: get_stream_playback returns null until
		// the stream is set.
		if (p_stream.is_valid()) {
			AudioStreamGenerator *g = dynamic_cast<AudioStreamGenerator *>(p_stream.ptr_raw());
			if (g) {
				Ref<AudioStreamGeneratorPlayback> gp = g->instantiate_playback();
				playback = Ref<AudioStreamPlayback>(
						static_cast<AudioStreamPlayback *>(gp.ptr_raw()));
			}
		}
	}
	Ref<AudioStream> get_stream() const { return stream; }

	void set_bus(const StringName &p_bus) { bus_name = p_bus; }
	StringName get_bus() const { return bus_name; }

	// Direct API (the engine bind also exposes these as methods).
	void play(float p_position = 0.0f) {
		playback_position = p_position;
		playing = true;
	}
	void stop() {
		playing = false;
		playback_position = 0.0f;
	}
	float get_playback_position() const { return playback_position; }
	Ref<AudioStreamPlayback> get_stream_playback() const { return playback; }
	bool is_playing() const { return playing; }

	// Duck-typed dispatch — covers the three methods godot-speech
	// calls through `call()`.
	bool has_method(const StringName &p_method) const override {
		const String m = (String)p_method;
		return m == String("play") || m == String("get_playback_position") ||
				m == String("get_stream_playback");
	}

	Variant call(const StringName &p_method) override {
		const String m = (String)p_method;
		if (m == String("get_playback_position")) {
			return Variant(playback_position);
		}
		if (m == String("get_stream_playback")) {
			return Variant(playback);
		}
		return Variant();
	}

	Variant call(const StringName &p_method, const Variant &p_arg0) override {
		const String m = (String)p_method;
		if (m == String("play")) {
			play(static_cast<float>(p_arg0));
			return Variant();
		}
		// Single-arg `get_stream_playback`/`get_playback_position`
		// fall through to the zero-arg overload behavior.
		return call(p_method);
	}
};

class AudioStreamPlayer : public AudioStreamPlayerCommon {};
class AudioStreamPlayer2D : public AudioStreamPlayerCommon {};
class AudioStreamPlayer3D : public AudioStreamPlayerCommon {};
