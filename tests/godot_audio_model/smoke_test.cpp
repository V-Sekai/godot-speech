// CHI-101 Phase A pass-3/4 smoke test:
//   - (pass 3) constructs the AudioDriver / CoreAudio driver and
//     prints its name + speaker mode
//   - (pass 4) exercises AudioServer + AudioEffectCapture: registers
//     a "Capture" bus with an AudioEffectCapture, pushes 100
//     synthetic stereo frames, then drains them and verifies the
//     round-trip preserves bit-exact values.

#include "audio/audio_driver.h"
#include "audio/audio_effect_capture.h"
#include "audio/audio_server.h"
#include "core/core_types.h"

#include <cstdio>

#ifdef COREAUDIO_ENABLED
#include "coreaudio/audio_driver_coreaudio.h"
#endif

static int test_capture_roundtrip() {
	AudioServer server;
	const int bus = server.test_add_bus(String("Capture"));

	Ref<AudioEffectCapture> capture = new AudioEffectCapture();
	capture->set_mix_rate(48000);
	capture->set_buffer_length(0.1f); // 100 ms, 4800 frames @ 48 kHz
	server.test_add_bus_effect(bus, capture);

	std::printf("audio_model smoke: bus index = %d\n",
			server.get_bus_index(StringName("Capture")));
	std::printf("audio_model smoke: bus effect count = %d\n",
			server.get_bus_effect_count(bus));
	std::printf("audio_model smoke: buffer_length_frames = %d\n",
			capture->get_buffer_length_frames());

	// Push 100 distinct stereo frames.
	PackedVector2Array in;
	in.resize(100);
	for (int i = 0; i < 100; ++i) {
		in[i] = Vector2(static_cast<float>(i), static_cast<float>(-i));
	}
	capture->push_test_frames(in);

	const int avail = capture->get_frames_available();
	std::printf("audio_model smoke: frames_available after push = %d\n", avail);
	if (avail != 100) {
		std::fprintf(stderr, "FAIL: expected 100 frames available, got %d\n", avail);
		return 1;
	}

	// Drain.
	PackedVector2Array out = capture->get_buffer(100);
	if (out.size() != 100) {
		std::fprintf(stderr, "FAIL: get_buffer(100) returned %d frames\n", out.size());
		return 1;
	}
	for (int i = 0; i < 100; ++i) {
		if (out[i].x != static_cast<float>(i) || out[i].y != static_cast<float>(-i)) {
			std::fprintf(stderr,
					"FAIL: frame %d round-trip mismatch: got (%g,%g) expected (%g,%g)\n",
					i, out[i].x, out[i].y, static_cast<float>(i), static_cast<float>(-i));
			return 1;
		}
	}
	std::printf("audio_model smoke: capture round-trip OK (100 frames bit-exact)\n");
	std::printf("audio_model smoke: pushed_frames = %lld, discarded_frames = %lld\n",
			(long long)capture->get_pushed_frames(),
			(long long)capture->get_discarded_frames());
	return 0;
}

int main() {
#ifdef COREAUDIO_ENABLED
	AudioDriverCoreAudio driver;
	driver.set_singleton();
	std::printf("audio_model smoke: driver name = %s\n", driver.get_name());
	std::printf("audio_model smoke: speaker mode = %d\n",
			static_cast<int>(driver.get_speaker_mode()));
#else
	std::printf("audio_model smoke: no concrete driver on this platform (CoreAudio is macOS-only)\n");
#endif

	return test_capture_roundtrip();
}
