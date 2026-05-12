// CHI-101 Phase A pass-4 — `RingBuffer<T>` stand-in.
//
// Mirrors the subset of core/templates/ring_buffer.h that
// AudioEffectCapture uses:
//
//   resize(size_pow2)   — capacity must be a power of two; the
//                         argument is the log2 of the desired
//                         capacity (matches engine semantics).
//   data_left()         — bytes/frames currently in the buffer
//   space_left()        — bytes/frames available for write
//   write(src, n)       — append n frames; on overflow, behavior
//                         differs from the engine: we discard
//                         oldest frames (FIFO drop) and increment
//                         an external counter that the caller
//                         (AudioEffectCapture) maintains via
//                         SafeNumeric<uint64_t> discarded_frames.
//                         The engine's RingBuffer itself does not
//                         drop — callers check space_left() first
//                         and choose. We preserve that contract:
//                         callers must check; `write` returns the
//                         number of frames written.
//   read(dst, n)        — pop n frames into dst; returns frames read.
//   clear()             — reset to empty.
//
// Indices are masked against `size() - 1` (cheap because size is
// a power of two).

#pragma once

#include "error_macros.h"
#include "typedefs.h"

#include <vector>

template <typename T>
class RingBuffer {
	std::vector<T> data;
	int read_pos = 0;
	int write_pos = 0;
	int size_mask = 0;

public:
	RingBuffer() = default;

	// Resize to 2^p_power frames. Matches engine convention.
	void resize(int p_power) {
		const int n = 1 << p_power;
		data.assign(static_cast<size_t>(n), T{});
		size_mask = n - 1;
		read_pos = 0;
		write_pos = 0;
	}

	int size() const { return static_cast<int>(data.size()); }

	int data_left() const {
		return (write_pos - read_pos) & size_mask;
	}

	int space_left() const {
		return size() - data_left() - 1;
	}

	// Write up to p_n frames; returns frames actually written.
	int write(const T *src, int p_n) {
		const int can = space_left();
		const int n = (p_n < can) ? p_n : can;
		for (int i = 0; i < n; ++i) {
			data[write_pos] = src[i];
			write_pos = (write_pos + 1) & size_mask;
		}
		return n;
	}

	// Read up to p_n frames; returns frames actually read.
	int read(T *dst, int p_n) {
		const int avail = data_left();
		const int n = (p_n < avail) ? p_n : avail;
		for (int i = 0; i < n; ++i) {
			dst[i] = data[read_pos];
			read_pos = (read_pos + 1) & size_mask;
		}
		return n;
	}

	void clear() {
		read_pos = 0;
		write_pos = 0;
	}
};
