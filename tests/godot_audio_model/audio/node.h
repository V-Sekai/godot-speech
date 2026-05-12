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
	void add_child(Node *p_child) {
		if (!p_child) {
			return;
		}
		p_child->parent = this;
		children.push_back(p_child);
	}

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
};

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
