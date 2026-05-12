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
#include "audio/audio_stream_generator.h"
#include "audio/audio_stream_player.h"
#include "audio/node.h"
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

static int test_playback_roundtrip() {
	// Allocate a 100 ms playback ring at 48 kHz.
	Ref<AudioStreamGenerator> gen = new AudioStreamGenerator();
	gen->set_mix_rate(48000.0f);
	gen->set_buffer_length(0.1f);
	Ref<AudioStreamGeneratorPlayback> playback = gen->instantiate_playback();

	const int cap = playback->get_total_capacity();
	std::printf("audio_model smoke: playback capacity = %d frames (%.1f ms)\n",
			cap, 1000.0f * cap / 48000.0f);

	const int initial_room = playback->get_frames_available();
	if (initial_room != cap - 1) {
		std::fprintf(stderr,
				"FAIL: expected free room = %d, got %d (ring keeps 1 sentinel slot)\n",
				cap - 1, initial_room);
		return 1;
	}

	// Push 480 frames (one 10ms voice packet at 48kHz). All distinct
	// so we can verify byte-for-byte preservation after draining.
	PackedVector2Array pkt;
	pkt.resize(480);
	for (int i = 0; i < 480; ++i) {
		pkt[i] = Vector2(static_cast<float>(i) * 0.001f, static_cast<float>(-i) * 0.001f);
	}
	if (!playback->push_buffer(pkt)) {
		std::fprintf(stderr, "FAIL: push_buffer rejected a 480-frame packet into an empty ring\n");
		return 1;
	}
	std::printf("audio_model smoke: after push 480: frames_available = %d, skips = %d\n",
			playback->get_frames_available(), playback->get_skips());

	// Drain 480 frames and confirm bit-exact preservation.
	PackedVector2Array drained = playback->consume_test_frames_into(480);
	if (drained.size() != 480) {
		std::fprintf(stderr, "FAIL: drained %d frames, expected 480\n", drained.size());
		return 1;
	}
	for (int i = 0; i < 480; ++i) {
		const float ex = static_cast<float>(i) * 0.001f;
		const float ey = static_cast<float>(-i) * 0.001f;
		if (drained[i].x != ex || drained[i].y != ey) {
			std::fprintf(stderr, "FAIL: drained[%d] = (%g, %g), expected (%g, %g)\n",
					i, drained[i].x, drained[i].y, ex, ey);
			return 1;
		}
	}

	// Now overflow: try to push more than the ring holds, verify
	// push_buffer returns false and skips increments.
	PackedVector2Array oversize;
	oversize.resize(cap + 100);
	for (int i = 0; i < cap + 100; ++i) {
		oversize[i] = Vector2(1.0f, 1.0f);
	}
	const int skips_before = playback->get_skips();
	const bool ok = playback->push_buffer(oversize);
	const int skips_after = playback->get_skips();
	if (ok) {
		std::fprintf(stderr, "FAIL: oversize push_buffer returned true\n");
		return 1;
	}
	if (skips_after != skips_before + 1) {
		std::fprintf(stderr,
				"FAIL: skips counter %d -> %d after overflow (expected +1)\n",
				skips_before, skips_after);
		return 1;
	}

	std::printf("audio_model smoke: playback round-trip OK (480 frames bit-exact + overflow skip detected)\n");
	return 0;
}

static int test_player_polymorphism() {
	// Build the player like Speech::add_player_audio would: an
	// AudioStreamPlayer node with an AudioStreamGenerator stream.
	AudioStreamPlayer player;
	player.set_name(String("AudioStreamPlayer"));
	player.set_bus(StringName("Master"));

	Ref<AudioStreamGenerator> gen = new AudioStreamGenerator();
	gen->set_mix_rate(48000.0f);
	gen->set_buffer_length(0.1f);
	player.set_stream(gen);

	// cast_to should resolve to all three player tags (the test
	// driver's parent walks the union of types).
	Node *as_node = &player;
	if (!cast_to<AudioStreamPlayer>(as_node)) {
		std::fprintf(stderr, "FAIL: cast_to<AudioStreamPlayer> returned null\n");
		return 1;
	}
	if (cast_to<AudioStreamPlayer2D>(as_node)) {
		std::fprintf(stderr, "FAIL: cast_to<AudioStreamPlayer2D> succeeded on AudioStreamPlayer\n");
		return 1;
	}
	if (cast_to<AudioStreamPlayer3D>(as_node)) {
		std::fprintf(stderr, "FAIL: cast_to<AudioStreamPlayer3D> succeeded on AudioStreamPlayer\n");
		return 1;
	}

	// has_method check (Speech does this before call("get_stream_playback")).
	if (!as_node->has_method(StringName("get_stream_playback"))) {
		std::fprintf(stderr, "FAIL: has_method(\"get_stream_playback\") returned false\n");
		return 1;
	}

	// call("play", float) → no-op return; play state updates.
	as_node->call(StringName("play"), Variant(0.25f));

	// call("get_playback_position") → Variant(float)
	Variant pos_var = as_node->call(StringName("get_playback_position"));
	const float pos = static_cast<float>(pos_var);
	if (pos != 0.25f) {
		std::fprintf(stderr, "FAIL: playback_position after play(0.25) = %g\n", pos);
		return 1;
	}

	// call("get_stream_playback") → Variant(Ref<AudioStreamPlayback>)
	// which implicitly converts to Ref<AudioStreamGeneratorPlayback>.
	Variant pb_var = as_node->call(StringName("get_stream_playback"));
	Ref<AudioStreamGeneratorPlayback> pb = pb_var;
	if (pb.is_null()) {
		std::fprintf(stderr, "FAIL: get_stream_playback returned null Ref\n");
		return 1;
	}

	// The returned playback must work end-to-end.
	PackedVector2Array pkt;
	pkt.resize(64);
	for (int i = 0; i < 64; ++i) {
		pkt[i] = Vector2(0.5f, -0.5f);
	}
	if (!pb->push_buffer(pkt)) {
		std::fprintf(stderr, "FAIL: push_buffer via Variant-derived playback failed\n");
		return 1;
	}

	std::printf("audio_model smoke: player polymorphism OK (cast_to + has_method + call -> Variant -> Ref<T>)\n");
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

	if (int rc = test_capture_roundtrip()) {
		return rc;
	}
	if (int rc = test_playback_roundtrip()) {
		return rc;
	}
	return test_player_polymorphism();
}
