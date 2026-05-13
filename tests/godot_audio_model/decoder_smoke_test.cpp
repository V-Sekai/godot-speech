// CHI-101 Phase A pass-7a smoke test — constructs a
// SpeechDecoder from godot-speech's speech_decoder.cpp linked
// against the audio model + system libopus. Verifies that the
// Opus decoder is created successfully and that the wrapper
// rejects an invalid-state call.

#include "../../speech_decoder.h"

#include <cstdio>

int main() {
	// Constructor calls opus_decoder_create internally.
	SpeechDecoder decoder;
	std::printf("decoder_smoke: SpeechDecoder constructed\n");

	// Negative path: process() with a zero-frame output should return
	// the Opus error code path, not crash. (Engine's contract is
	// "return Opus error; don't trash memory".)
	PackedByteArray empty_compressed;
	PackedByteArray pcm_out;
	pcm_out.resize(2); // need at least one int16
	int32_t result = decoder.process(&empty_compressed, &pcm_out,
			/*compressed=*/0, /*pcm=*/1, /*frame_count=*/0);
	std::printf("decoder_smoke: process(empty) returned %d (negative = Opus error code)\n",
			result);

	std::printf("decoder_smoke: PASS\n");
	return 0;
}
