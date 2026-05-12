// CHI-101 Phase A pass-3 — engine-path shim for `OS::get_singleton()`.
// The CoreAudio driver uses OS only for `get_ticks_msec` (debug
// timing) and `get_name` (string log decoration). We return a
// monotonic millisecond clock and a fixed name.
#pragma once
#include "../../../core/core_types.h"

#include <chrono>

class OS {
	static OS instance;

public:
	static OS *get_singleton() { return &instance; }

	uint64_t get_ticks_msec() const {
		using clock = std::chrono::steady_clock;
		static const auto epoch = clock::now();
		return std::chrono::duration_cast<std::chrono::milliseconds>(
				clock::now() - epoch)
				.count();
	}

	String get_name() const { return String("audio_model"); }
};

inline OS OS::instance{};
