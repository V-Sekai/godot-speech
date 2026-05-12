// CHI-101 Phase A pass-3 — Godot core typedefs / compiler macros.
// Minimum subset of core/typedefs.h needed by the AudioDriver +
// CoreAudio surface.

#pragma once

#include <cstddef>
#include <cstdint>

#if defined(__GNUC__) || defined(__clang__)
#define _FORCE_INLINE_ __attribute__((always_inline)) inline
#define _ALWAYS_INLINE_ __attribute__((always_inline)) inline
#define _NO_INLINE_ __attribute__((noinline))
#else
#define _FORCE_INLINE_ inline
#define _ALWAYS_INLINE_ inline
#define _NO_INLINE_
#endif

#ifndef SIZE_MAX
#include <climits>
#endif

#ifndef MIN
#define MIN(a, b) (((a) < (b)) ? (a) : (b))
#endif

#ifndef MAX
#define MAX(a, b) (((a) > (b)) ? (a) : (b))
#endif

#ifndef CLAMP
#define CLAMP(value, lo, hi) (((value) < (lo)) ? (lo) : (((value) > (hi)) ? (hi) : (value)))
#endif
