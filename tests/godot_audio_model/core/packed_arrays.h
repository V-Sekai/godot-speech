// CHI-101 Phase A pass-3 — `PackedByteArray` and `PackedFloat32Array`.
// Same surface as core/variant/variant.h's typed packed arrays.
// std::vector-backed; no COW.

#pragma once

#include "typedefs.h"

#include <vector>

class PackedByteArray {
	std::vector<uint8_t> v;

public:
	PackedByteArray() = default;
	explicit PackedByteArray(int n) :
			v(n) {}

	void resize(int n) { v.resize(static_cast<size_t>(n)); }
	int size() const { return static_cast<int>(v.size()); }
	bool is_empty() const { return v.empty(); }
	void fill(uint8_t val) {
		for (auto &x : v)
			x = val;
	}
	void clear() { v.clear(); }

	const uint8_t *ptr() const { return v.data(); }
	uint8_t *ptrw() { return v.data(); }

	uint8_t operator[](int i) const { return v[i]; }
	uint8_t &operator[](int i) { return v[i]; }

	// Engine API: `slice(int begin, int end)` returns a copy of the
	// half-open range [begin, end). end defaults to size().
	PackedByteArray slice(int begin, int end = -1) const {
		PackedByteArray out;
		const int sz = size();
		const int e = (end < 0) ? sz : ((end > sz) ? sz : end);
		const int b = (begin < 0) ? 0 : ((begin > e) ? e : begin);
		out.v.assign(v.begin() + b, v.begin() + e);
		return out;
	}
};

class PackedFloat32Array {
	std::vector<float> v;

public:
	PackedFloat32Array() = default;
	explicit PackedFloat32Array(int n) :
			v(n) {}

	void resize(int n) { v.resize(static_cast<size_t>(n)); }
	int size() const { return static_cast<int>(v.size()); }
	bool is_empty() const { return v.empty(); }
	void fill(float val) {
		for (auto &x : v)
			x = val;
	}
	void clear() { v.clear(); }

	const float *ptr() const { return v.data(); }
	float *ptrw() { return v.data(); }

	float operator[](int i) const { return v[i]; }
	float &operator[](int i) { return v[i]; }
};
