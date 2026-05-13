// CHI-101 Phase A pass-6 — `Node` base class + `cast_to<T>`.
//
// The reference engine's Node carries a heavy stack of
// signal/property/group/RPC machinery; we keep only the parts
// godot-speech actually touches:
//
//   * parent/child pointers (so `get_node_or_null(NodePath("…"))`
//     can walk a tree the test driver builds)
//   * `call(StringName method, …)` returning Variant — duck-typed
//     method dispatch
//   * `has_method(StringName)` predicate
//   * a virtual destructor so dynamic_cast (and our `cast_to<T>`)
//     works on the inheritance hierarchy
//
// Nothing here is RefCounted — the engine's Node owns its
// children directly. Lifetime in the test binary is handled by
// the test driver (stack scope or unique_ptr).

#pragma once

#include "../core/core_types.h"
#include "../core/variant.h"
#include "multiplayer.h"

#include <vector>

class Node {
	String node_name;
	Node *parent = nullptr;
	std::vector<Node *> children;

public:
	virtual ~Node() = default;

	void set_name(const String &p_name) { node_name = p_name; }
	String get_name() const { return node_name; }

	// Tree manipulation (test-driver only; the engine has a
	// scene-tree state machine we don't model).
	// Engine has 1-arg + 2-arg overloads; latter accepts a
	// force-readable-name flag that the test path ignores.
	void add_child(Node *p_child, bool /*p_force_readable_name*/ = false) {
		if (!p_child) {
			return;
		}
		p_child->parent = this;
		children.push_back(p_child);
	}

	void remove_child(Node *p_child) {
		for (auto it = children.begin(); it != children.end(); ++it) {
			if (*it == p_child) {
				children.erase(it);
				if (p_child->parent == this) {
					p_child->parent = nullptr;
				}
				return;
			}
		}
	}

	void set_owner(Node * /*p_owner*/) {}
	Node *get_owner() const { return nullptr; }

	void set_process_internal(bool /*p_enable*/) {}

	// Engine `queue_free` requests deletion at end of frame. The
	// test binary has no frame loop; queue_free is a no-op and
	// the test driver is responsible for memdelete lifetimes.
	void queue_free() {}

	// Per-frame delta — test driver advances time manually.
	float get_process_delta_time() const { return 1.0f / 60.0f; }

	Node *get_node_or_null(const NodePath &p_path) {
		const String target = p_path.get_path();
		for (Node *c : children) {
			if (c->node_name == target) {
				return c;
			}
		}
		return nullptr;
	}

	// Duck-typed call interface. Derived classes override.
	virtual bool has_method(const StringName & /*p_method*/) const { return false; }
	virtual Variant call(const StringName & /*p_method*/) { return Variant(); }
	virtual Variant call(const StringName & /*p_method*/,
			const Variant & /*p_arg0*/) {
		return Variant();
	}

	// `emit_signal` — engine fans the signal out to connected
	// handlers. The test path has no signal listeners (the engine
	// scene graph isn't running); we accept the call as a no-op
	// and silently drop the payload. If a future test needs to
	// observe emitted signals, route via a per-Node callback list.
	template <typename... Args>
	void emit_signal(const StringName & /*p_name*/, Args &&...) const {}

	// `_notification(int p_what)` — virtual lifecycle hook the
	// engine calls during scene-tree state changes. Test path
	// drives this manually when needed.
	virtual void _notification(int /*p_what*/) {}

	// `get_tree()` — engine returns the SceneTree singleton when
	// the Node is in the tree. Test binary returns a single global
	// SceneTree that test drivers can configure via
	// `MultiplayerAPI::test_attach_peer`.
	SceneTree *get_tree() const;
};

// Storage + access for the global SceneTree the test driver
// configures. Defined in node.cpp (out-of-line so audio_driver.cpp
// alone owns the symbol).
SceneTree *_audio_model_get_or_create_scene_tree();
inline SceneTree *Node::get_tree() const {
	return _audio_model_get_or_create_scene_tree();
}

// Engine NOTIFICATION_* constants — the few godot-speech reacts to.
enum {
	NOTIFICATION_ENTER_TREE = 10,
	NOTIFICATION_READY = 11,
	NOTIFICATION_EXIT_TREE = 12,
	NOTIFICATION_PROCESS = 13,
	NOTIFICATION_PHYSICS_PROCESS = 14,
	NOTIFICATION_INTERNAL_PROCESS = 15,
	NOTIFICATION_POSTINITIALIZE = 16,
};

// Node lifecycle setters — engine controls whether `_notification`
// fires per-frame. Test path drives manually; setters are no-ops.
inline void set_process(bool /*p_active*/) {}
inline void set_physics_process(bool /*p_active*/) {}
inline void set_process_input(bool /*p_active*/) {}

// `real_t` — engine's switchable float/double precision type. We
// fix it to float since the model is fp32 throughout.
using real_t = float;

// Engine-style typed cast. The engine implements this via its own
// RTTI (registered via GDCLASS); dynamic_cast works for us because
// every class in the model has at least one virtual function.
template <typename T>
T *cast_to(Node *p_node) {
	return dynamic_cast<T *>(p_node);
}

template <typename T>
const T *cast_to(const Node *p_node) {
	return dynamic_cast<const T *>(p_node);
}

// `cast_to<T>(Variant)` is defined in core/variant.h — the
// declaration here is the engine's convention that the overload
// also works for Variant payloads.
