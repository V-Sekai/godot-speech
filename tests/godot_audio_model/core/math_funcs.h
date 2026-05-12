// CHI-101 Phase A pass-3 — Math:: helper subset.
// Mirrors core/math/math_funcs.h for the few entries godot-speech uses.

#pragma once

#include "typedefs.h"

#include <cmath>
#include <cstdlib>

namespace Math {

inline float abs(float v) {
	return std::fabs(v);
}
inline double abs(double v) {
	return std::fabs(v);
}
inline int abs(int v) {
	return std::abs(v);
}

inline float sin(float v) {
	return std::sin(v);
}
inline double sin(double v) {
	return std::sin(v);
}

inline float cos(float v) {
	return std::cos(v);
}
inline double cos(double v) {
	return std::cos(v);
}

inline float floor(float v) {
	return std::floor(v);
}
inline double floor(double v) {
	return std::floor(v);
}

inline bool is_zero_approx(float v) {
	return Math::abs(v) < 1e-6f;
}
inline bool is_zero_approx(double v) {
	return Math::abs(v) < 1e-12;
}

inline bool is_equal_approx(float a, float b) {
	return Math::abs(a - b) < 1e-6f * MAX(Math::abs(a), Math::abs(b)) + 1e-7f;
}

constexpr double PI = 3.14159265358979323846;

} // namespace Math
