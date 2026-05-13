// CHI-101 Phase A pass-7 — thirdparty-path shim for libopus.
// godot-speech's speech_processor.h / speech_decoder.h pull this in
// via the engine-build's "thirdparty/opus/opus/opus.h" convention.
// In the test binary we link against the system opus (brew on macOS,
// libopus-dev on Linux) at <opus/opus.h>.
#pragma once
#include <opus/opus.h>
