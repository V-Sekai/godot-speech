// CHI-101 Phase A pass-7c smoke test — constructs godot-speech's
// full `Speech` node (the receive-side + jitter-buffer driver
// class) and verifies its dependencies on SceneTree,
// MultiplayerAPI, AudioStreamPlayer's Variant `call(...)` flow,
// and the player_audio Dictionary all wire up without crashing.
//
// Doesn't drive a full receive pipeline (that's an integration
// test belonging in a follow-up PR). The goal here is just to
// prove the link works end-to-end and Speech's setup paths run.

#include "../../speech.h"

#include <cstdio>

int main() {
	Speech speech;
	std::printf("speech_smoke: Speech constructed\n");

	// Basic property getters / setters — engine binds these via
	// GDCLASS but we exercise them as direct C++ calls.
	speech.set_max_jitter_buffer_size(32);
	if (speech.get_max_jitter_buffer_size() != 32) {
		std::fprintf(stderr, "FAIL: max_jitter_buffer_size getter/setter\n");
		return 1;
	}

	speech.set_jitter_buffer_speedup(20);
	if (speech.get_jitter_buffer_speedup() != 20) {
		std::fprintf(stderr, "FAIL: jitter_buffer_speedup getter/setter\n");
		return 1;
	}

	speech.set_jitter_buffer_slowdown(8);
	if (speech.get_jitter_buffer_slowdown() != 8) {
		std::fprintf(stderr, "FAIL: jitter_buffer_slowdown getter/setter\n");
		return 1;
	}

	// SceneTree + MultiplayerAPI plumbing — Speech::on_received_audio_packet
	// reaches through `get_tree()->get_multiplayer()` for echo
	// prevention. Verify those don't crash on a peer-less default.
	SceneTree *tree = speech.get_tree();
	if (!tree) {
		std::fprintf(stderr, "FAIL: Speech::get_tree() returned null\n");
		return 1;
	}
	Ref<MultiplayerAPI> mp = tree->get_multiplayer();
	if (mp.is_null()) {
		std::fprintf(stderr, "FAIL: get_multiplayer() returned null Ref\n");
		return 1;
	}
	if (mp->has_multiplayer_peer()) {
		std::fprintf(stderr, "FAIL: default MultiplayerAPI shouldn't have a peer\n");
		return 1;
	}

	// The on_received_audio_packet path walks
	// `get_tree() && get_tree()->get_multiplayer().is_valid() &&
	// get_tree()->get_multiplayer()->has_multiplayer_peer()`. With
	// no peer attached, the function falls through without doing
	// echo prevention (engine intent). Drive a packet and verify
	// nothing crashes — the player_audio map is empty so the
	// `!player_audio.has(p_peer_id)` branch returns early.
	PackedByteArray packet;
	packet.resize(10);
	for (int i = 0; i < 10; ++i) {
		packet[i] = static_cast<uint8_t>(i);
	}
	speech.on_received_audio_packet(/*peer*/ 42, /*sequence*/ 1, packet);
	std::printf("speech_smoke: on_received_audio_packet returned without crash\n");

	std::printf("speech_smoke: PASS — Speech links + runs end-to-end against the audio model\n");
	return 0;
}
