// CHI-101 Phase A pass-3 smoke test — constructs the AudioDriver
// singleton, queries its `get_name()`, and (on macOS) instantiates
// the CoreAudio driver. Doesn't actually start audio I/O — just
// verifies the model + driver compile and link.

#include "audio/audio_driver.h"
#include "core/core_types.h"

#include <cstdio>

#ifdef COREAUDIO_ENABLED
#include "coreaudio/audio_driver_coreaudio.h"
#endif

int main() {
#ifdef COREAUDIO_ENABLED
	AudioDriverCoreAudio driver;
	driver.set_singleton();
	std::printf("audio_model smoke: driver name = %s\n", driver.get_name());
	std::printf("audio_model smoke: speaker mode = %d\n",
			static_cast<int>(driver.get_speaker_mode()));
	// init() / start() would actually open the OS audio device; we
	// stop at the construct-and-query stage for the smoke test.
	return 0;
#else
	std::printf("audio_model smoke: no concrete driver on this platform (CoreAudio is macOS-only)\n");
	return 0;
#endif
}
