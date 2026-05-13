// CHI-101 Phase A pass-7c — global SceneTree storage for the test
// binary. One process-wide SceneTree owns a default-constructed
// MultiplayerAPI; `Node::get_tree()` returns this for every node.
// Test drivers reach the MultiplayerAPI via
// `node->get_tree()->get_multiplayer()` and call the test-only
// helpers (`test_attach_peer`, `test_set_remote_sender`) to drive
// echo-prevention scenarios.

#include "node.h"

static SceneTree *g_scene_tree = nullptr;

SceneTree *_audio_model_get_or_create_scene_tree() {
	if (!g_scene_tree) {
		g_scene_tree = new SceneTree();
	}
	return g_scene_tree;
}
