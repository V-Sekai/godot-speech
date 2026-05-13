// CHI-101 Phase A pass-7c — minimal `MultiplayerAPI` + `SceneTree`
// stand-ins.
//
// godot-speech walks the multiplayer-side stack on the receive
// path only for echo prevention:
//
//   get_tree()->get_multiplayer().is_valid() &&
//   get_tree()->get_multiplayer()->has_multiplayer_peer()
//   ->get_unique_id() / ->get_remote_sender_id()
//
// In the test binary there is no real multiplayer transport — we
// return a SceneTree with a default-constructed MultiplayerAPI
// that reports `has_multiplayer_peer() = false`, which collapses
// the echo-prevention branch to the no-op fallthrough path
// godot-speech already supports (see speech.cpp:579-590 — the
// `else if (DEBUG && ...)` branch).

#pragma once

#include "../core/core_types.h"

class MultiplayerAPI : public RefCounted {
	bool peer_attached = false;
	int unique_id = 1;
	int remote_sender_id = 0;

public:
	bool has_multiplayer_peer() const { return peer_attached; }
	int get_unique_id() const { return unique_id; }
	int get_remote_sender_id() const { return remote_sender_id; }

	// Test-only helpers — drive the API from outside the test.
	void test_attach_peer(int p_unique_id) {
		peer_attached = true;
		unique_id = p_unique_id;
	}
	void test_set_remote_sender(int p_sender_id) {
		remote_sender_id = p_sender_id;
	}
};

class SceneTree {
	Ref<MultiplayerAPI> multiplayer;

public:
	SceneTree() { multiplayer = Ref<MultiplayerAPI>(new MultiplayerAPI()); }

	Ref<MultiplayerAPI> get_multiplayer() const { return multiplayer; }
};
