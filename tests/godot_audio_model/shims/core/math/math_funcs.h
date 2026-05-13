#pragma once
#include "../../../core/math_funcs.h"

namespace Math {
inline float absf(float v) {
	return v < 0.0f ? -v : v;
}
inline double absf(double v) {
	return v < 0.0 ? -v : v;
}
} // namespace Math
