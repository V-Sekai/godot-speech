// CHI-101 Phase A pass-3 — engine-path shim for `Engine::get_singleton()`.
// The CoreAudio driver only reads `get_audio_output_latency()`; we
// return a fixed default since the test binary has no project settings.
#pragma once
#include "../../../core/core_types.h"

class Engine {
	static Engine instance;

public:
	static Engine *get_singleton() { return &instance; }

	uint32_t get_audio_output_latency() const {
		// 25 ms is Godot's default audio_output_latency project setting.
		return 25;
	}
};

inline Engine Engine::instance{};
