// CHI-101 Phase A pass-3 — engine-path shim. The CoreAudio driver
// uses `GLOBAL_GET("…")` to read project settings (enable_input,
// audio-driver tuning knobs). The reference engine resolves these
// against `ProjectSettings::get_singleton()`; the test binary has
// no project settings, so the macro returns hard-coded defaults
// chosen to keep the driver in its "normal" mode.
#pragma once

#include "../../../core/core_types.h"

#include <cstring>

namespace _audio_model {
// Returns a default value for the few project-setting keys the
// CoreAudio driver consults. Unknown keys return 0.
inline int global_get_int(const char *key) {
	if (std::strcmp(key, "audio/driver/enable_input") == 0) {
		return 1; // capture path needs to be on for the speech tests
	}
	return 0;
}
} // namespace _audio_model

#define GLOBAL_GET(m_key) (_audio_model::global_get_int(m_key))
