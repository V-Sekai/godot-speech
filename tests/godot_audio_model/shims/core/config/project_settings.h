// CHI-101 Phase A pass-3/7b — engine-path shim. Provides
// `ProjectSettings::get_singleton()->get(...)` and the engine
// `GLOBAL_GET(...)` macro by routing both to a hard-coded
// "audio/driver/enable_input = 1" + "everything else = 0" table.
// The test binary has no real ProjectSettings; this just keeps the
// godot-speech configuration-checks falling through to the engaged
// path so capture initializes.

#pragma once

#include "../../../core/core_types.h"

#include <cstring>

namespace _audio_model {
inline int global_get_int(const char *key) {
	if (std::strcmp(key, "audio/driver/enable_input") == 0) {
		return 1; // capture path needs to be on for the speech tests
	}
	if (std::strcmp(key, "audio/enable_audio_input") == 0) {
		return 1; // speech_processor::start() checks this
	}
	return 0;
}
} // namespace _audio_model

#define GLOBAL_GET(m_key) (_audio_model::global_get_int(m_key))

class ProjectSettings {
	static ProjectSettings instance;

public:
	static ProjectSettings *get_singleton() { return &instance; }

	Variant get(const String &key) const {
		return Variant(_audio_model::global_get_int(key.utf8().get_data()));
	}
	Variant get(const char *key) const { return get(String(key)); }
};

inline ProjectSettings ProjectSettings::instance{};
