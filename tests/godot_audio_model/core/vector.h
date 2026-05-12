// CHI-101 Phase A pass-3 — minimal `Vector<T>` stand-in.
// Reference engine uses a COW vector in `core/templates/vector.h`;
// here we wrap std::vector and expose the same API surface
// godot-speech uses (`size`, `resize`, `ptr`, `ptrw`, `push_back`,
// `pop_front`, `fill`, `is_empty`, `clear`, `write` accessor).
//
// COW semantics aren't reproduced — every Vector owns its storage
// independently. That changes one observable behavior: copying a
// Vector eagerly clones the underlying data, where Godot's engine
// would defer until a write. For the test binary's workloads
// (small buffers, infrequent copies) the difference is invisible.

#pragma once

#include "typedefs.h"

#include <algorithm>
#include <cstring>
#include <vector>

template <typename T>
class Vector {
	std::vector<T> v;

public:
	struct Writer {
		std::vector<T> &v;
		T &operator[](int i) { return v[i]; }
	};

	Vector() = default;
	Vector(int n) :
			v(n) {}

	void resize(int n) { v.resize(static_cast<size_t>(n)); }
	void resize(int n, const T &val) { v.resize(static_cast<size_t>(n), val); }

	int size() const { return static_cast<int>(v.size()); }
	bool is_empty() const { return v.empty(); }
	void clear() { v.clear(); }

	void push_back(const T &x) { v.push_back(x); }
	void pop_back() {
		if (!v.empty())
			v.pop_back();
	}

	// Godot's Vector exposes pop_front but it's O(n); we keep parity.
	T pop_front() {
		T front = v.front();
		v.erase(v.begin());
		return front;
	}

	void fill(const T &val) { std::fill(v.begin(), v.end(), val); }

	const T *ptr() const { return v.data(); }
	T *ptrw() { return v.data(); }

	const T &operator[](int i) const { return v[i]; }
	T &operator[](int i) { return v[i]; }

	Writer write() { return Writer{ v }; }

	auto begin() { return v.begin(); }
	auto end() { return v.end(); }
	auto begin() const { return v.cbegin(); }
	auto end() const { return v.cend(); }
};
