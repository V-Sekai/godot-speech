// CHI-101 Phase A pass-4 — `AudioFrame` stand-in.
//
// Mirrors core/math/audio_frame.h's API: two-float stereo sample
// with `.left`/`.l`/`[0]` and `.right`/`.r`/`[1]` access, plus the
// arithmetic operators the engine's audio paths use. The engine
// uses a union for the named accessors; we use a small struct
// with both names as members to keep header parsing simple.
//
// `Vector2` and `AudioFrame` are interchangeable in memory layout
// for the cross-API points in godot-speech (capture/playback
// rings pass `PackedVector2Array`), so callers that want either
// view can `reinterpret_cast` between them.

#pragma once

#include "typedefs.h"

struct AudioFrame {
	float left = 0.0f;
	float right = 0.0f;

	AudioFrame() = default;
	constexpr AudioFrame(float p_l, float p_r) :
			left(p_l), right(p_r) {}

	float &l() { return left; }
	float &r() { return right; }
	const float &l() const { return left; }
	const float &r() const { return right; }

	_ALWAYS_INLINE_ float &operator[](int i) { return (i == 0) ? left : right; }
	_ALWAYS_INLINE_ const float &operator[](int i) const { return (i == 0) ? left : right; }

	constexpr AudioFrame operator+(const AudioFrame &o) const { return AudioFrame(left + o.left, right + o.right); }
	constexpr AudioFrame operator-(const AudioFrame &o) const { return AudioFrame(left - o.left, right - o.right); }
	constexpr AudioFrame operator*(const AudioFrame &o) const { return AudioFrame(left * o.left, right * o.right); }
	constexpr AudioFrame operator*(float s) const { return AudioFrame(left * s, right * s); }
	constexpr void operator+=(const AudioFrame &o) {
		left += o.left;
		right += o.right;
	}
	constexpr void operator*=(float s) {
		left *= s;
		right *= s;
	}
};
