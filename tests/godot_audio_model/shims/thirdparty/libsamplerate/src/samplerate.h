// CHI-101 Phase A pass-7 — thirdparty-path shim for libsamplerate.
// godot-speech includes this via "thirdparty/libsamplerate/src/samplerate.h".
// The system install (brew on macOS, libsamplerate0-dev on Linux)
// puts the header at <samplerate.h>.
#pragma once
#include <samplerate.h>
