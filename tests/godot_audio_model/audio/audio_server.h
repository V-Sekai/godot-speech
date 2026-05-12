// CHI-101 Phase A pass-4 — minimal `AudioServer` stand-in.
//
// godot-speech reaches the AudioServer through:
//
//   AudioServer::get_singleton()->get_input_mix_rate()
//   AudioServer::get_singleton()->get_bus_index(StringName)
//   AudioServer::get_singleton()->get_bus_effect_count(int)
//   AudioServer::get_singleton()->get_bus_effect(int, int) -> Ref<AudioEffect>
//
// `set_streaming_bus` walks the bus's effect list to find an
// AudioEffectCapture. The model exposes a test-only entry that
// lets the harness add buses with named effects without going
// through the full audio-driver setup.

#pragma once

#include "../core/core_types.h"
#include "audio_effect.h"
#include "audio_effect_capture.h"

#include <vector>

class AudioServer {
	static AudioServer *singleton;

	struct Bus {
		String name;
		std::vector<Ref<AudioEffect>> effects;
	};
	std::vector<Bus> buses;
	uint32_t input_mix_rate = 48000;

public:
	AudioServer() { singleton = this; }
	~AudioServer() {
		if (singleton == this) {
			singleton = nullptr;
		}
	}

	static AudioServer *get_singleton() { return singleton; }

	uint32_t get_input_mix_rate() const { return input_mix_rate; }
	void set_input_mix_rate(uint32_t p_rate) { input_mix_rate = p_rate; }

	int get_bus_index(const StringName &p_name) const {
		const String name = static_cast<String>(p_name);
		for (size_t i = 0; i < buses.size(); ++i) {
			if (buses[i].name == name) {
				return static_cast<int>(i);
			}
		}
		return -1;
	}

	int get_bus_effect_count(int p_bus) const {
		ERR_FAIL_INDEX_V(p_bus, static_cast<int>(buses.size()), 0);
		return static_cast<int>(buses[p_bus].effects.size());
	}

	Ref<AudioEffect> get_bus_effect(int p_bus, int p_idx) const {
		ERR_FAIL_INDEX_V(p_bus, static_cast<int>(buses.size()), Ref<AudioEffect>());
		ERR_FAIL_INDEX_V(p_idx, static_cast<int>(buses[p_bus].effects.size()), Ref<AudioEffect>());
		return buses[p_bus].effects[p_idx];
	}

	// === Test-only API (not in the engine) ====================
	// Add a bus with a list of effects. Returns the bus index.
	int test_add_bus(const String &p_name) {
		buses.push_back(Bus{ p_name, {} });
		return static_cast<int>(buses.size()) - 1;
	}

	void test_add_bus_effect(int p_bus, const Ref<AudioEffect> &p_effect) {
		ERR_FAIL_INDEX(p_bus, static_cast<int>(buses.size()));
		buses[p_bus].effects.push_back(p_effect);
	}
};
