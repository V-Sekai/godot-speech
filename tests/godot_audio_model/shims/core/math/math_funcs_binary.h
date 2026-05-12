// CHI-101 Phase A pass-3 — engine-path shim. The reference engine's
// `core/math/math_funcs_binary.h` declares Math:: bit-wise helpers
// (next_power_of_2, etc.). The CoreAudio driver pulls it in for
// `Math::range_lerp` / `Math::log2_int`; we re-export the parts it
// actually uses.
#pragma once
#include "../../../core/math_funcs.h"

namespace Math {

inline int next_power_of_2(int v) {
	if (v <= 1) {
		return 1;
	}
	int p = 1;
	while (p < v) {
		p <<= 1;
	}
	return p;
}

inline int closest_power_of_2(int v) {
	if (v <= 1) {
		return 1;
	}
	int nxt = next_power_of_2(v);
	int prv = nxt >> 1;
	return (v - prv) <= (nxt - v) ? prv : nxt;
}

} // namespace Math
