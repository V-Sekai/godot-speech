// CHI-101 Phase A pass-3 — minimal `Vector2` stand-in.
// Mirrors the public surface of core/math/vector2.h that
// godot-speech actually uses (read .x / .y, construct from a pair,
// stereo audio frame storage). No transform/rotation/length math
// reproduced.

#pragma once

#include "typedefs.h"

#include <vector>

struct Vector2 {
	float x = 0.0f;
	float y = 0.0f;

	Vector2() = default;
	Vector2(float p_x, float p_y) :
			x(p_x), y(p_y) {}
	explicit Vector2(float v) :
			x(v), y(v) {}

	Vector2 operator+(const Vector2 &o) const { return Vector2(x + o.x, y + o.y); }
	Vector2 operator-(const Vector2 &o) const { return Vector2(x - o.x, y - o.y); }
	Vector2 operator*(float s) const { return Vector2(x * s, y * s); }
	Vector2 &operator+=(const Vector2 &o) {
		x += o.x;
		y += o.y;
		return *this;
	}
};

class PackedVector2Array {
	std::vector<Vector2> v;

public:
	PackedVector2Array() = default;
	explicit PackedVector2Array(int n) :
			v(n) {}

	void resize(int n) { v.resize(static_cast<size_t>(n)); }
	int size() const { return static_cast<int>(v.size()); }
	bool is_empty() const { return v.empty(); }
	void fill(const Vector2 &val) {
		for (auto &x : v)
			x = val;
	}
	void clear() { v.clear(); }

	const Vector2 *ptr() const { return v.data(); }
	Vector2 *ptrw() { return v.data(); }

	const Vector2 &operator[](int i) const { return v[i]; }
	Vector2 &operator[](int i) { return v[i]; }

	void push_back(const Vector2 &x) { v.push_back(x); }
};
