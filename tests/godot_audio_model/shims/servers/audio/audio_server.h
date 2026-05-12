// CHI-101 Phase A pass-3 — engine-path shim. The verbatim CoreAudio
// driver in tests/godot_audio_model/coreaudio/ includes this path;
// re-export our minimal AudioDriver via the same header location.
#pragma once
#include "../../../audio/audio_driver.h"
