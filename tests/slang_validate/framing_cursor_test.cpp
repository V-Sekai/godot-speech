// Real-input validator for Speech.SlangCodegen.FramingCursor.
//
// Reference: lean/Speech/SlangCodegen/FramingCursor.lean. Encodes the
// capture-buffer framing-cursor policy. Per audio-thread tick with
// `newSamples[i]` newly-resampled samples available:
//
//   total           = carry + newSamples[i]
//   framesEmitted[i] = total / frameSize     // integer division
//   nextCarry       = total mod frameSize
//
// This harness has three jobs:
//   1. Verify the kernel matches the safe-math reference frame-for-frame
//      across a long, varied tick sequence (bit-exact on uint counts).
//   2. Verify state carry-over by splitting the sequence across two
//      dispatches.
//   3. Demonstrate the divergence between the kernel (safe) and the
//      buggy loop currently in speech_processor.cpp:130 (unsigned
//      subtraction with underflow). The bug case fires when
//      `carry + newSamples < frameSize` on a tick where the original
//      C++ would compute `resampled_frame_count - frameSize` and
//      underflow.
//
// Job 3 is the entire reason this kernel exists — it pins the first
// concrete diff between speech_processor.cpp and the Lean spec.

#include <chrono>
#include <cstdint>
#include <cstdio>

#include "framing_cursor_emit.cpp"

static constexpr double kBudgetSeconds = 5.0;

// Lean-spec-faithful reference: integer divmod arithmetic.
static void reference_policy(
		const uint32_t *newSamples, uint32_t numTicks, uint32_t frameSize,
		uint32_t &carry, uint32_t *framesEmitted) {
	for (uint32_t i = 0; i < numTicks; ++i) {
		const uint32_t total = carry + newSamples[i];
		framesEmitted[i] = total / frameSize;
		carry = total % frameSize;
	}
}

// Literal translation of the speech_processor.cpp:130 loop, including
// the unsigned-subtraction bug. Used in Job 3 to demonstrate the diff.
//
// CAVEAT: in the underflow case this loop would run effectively
// forever in the real engine. We cap iteration here at a generous
// fuse so the test terminates and we can report the divergence
// without hanging CI.
static constexpr uint32_t kBuggyLoopFuse = 1'000'000u;

struct BuggyResult {
	uint32_t framesEmitted;
	bool hit_fuse;
};

static BuggyResult buggy_speech_processor_cpp_loop(
		uint32_t prior_carry, uint32_t newSamples, uint32_t frameSize) {
	// total samples available = prior_carry + newSamples.
	// speech_processor.cpp sets resampled_frame_count = prior_carry +
	// (resampler return); then resets offset = 0 and loops:
	//   while (offset < resampled_frame_count - frameSize) {
	//     emit; offset += frameSize;
	//   }
	uint32_t resampled_frame_count = prior_carry + newSamples;
	uint32_t offset = 0u;
	uint32_t frames = 0u;
	uint32_t fuse = 0u;
	while (offset < resampled_frame_count - frameSize) { // ← unsigned underflow
		offset += frameSize;
		++frames;
		if (++fuse > kBuggyLoopFuse) {
			return { frames, true };
		}
	}
	return { frames, false };
}

// Run the kernel over `count` ticks starting at `newSamples`, with
// stateIO carried across dispatches.
static void dispatch_kernel(
		const uint32_t *newSamples, uint32_t *framesEmitted,
		uint32_t count, uint32_t frameSize, uint32_t *stateIO) {
	FramingCursorParams_0 params{ count, frameSize };
	GlobalParams_0 gp{};
	gp.params_0 = &params;
	gp.newSamples_0.data = const_cast<uint32_t *>(newSamples);
	gp.newSamples_0.count = count;
	gp.framesEmitted_0.data = framesEmitted;
	gp.framesEmitted_0.count = count;
	gp.stateIO_0.data = stateIO;
	gp.stateIO_0.count = 1;

	ComputeVaryingInput vi{};
	vi.startGroupID = uint3(0, 0, 0);
	vi.endGroupID = uint3(1, 1, 1);
	main_0(&vi, nullptr, &gp);
}

int main() {
	const auto t0 = std::chrono::steady_clock::now();

	constexpr uint32_t FRAME_SIZE = 480u;

	// ---- Job 1: long varied sequence, kernel vs reference.
	//
	// Mix of mid-size, large, exact-multiple, and tiny ticks. The tiny
	// ticks (e.g. 128) test the "below-frameSize" case without
	// triggering the bug (they accumulate over multiple ticks until a
	// frame can be emitted).
	const uint32_t ticks[] = {
		2048,
		2048,
		2048, // happy path: 4-5 frames each tick
		960,
		1920, // exact multiples of 480
		128,
		128,
		128, // tiny — 3 ticks before first frame emit
		47,
		4801, // odd, prime-ish
		512,
		479,
		1, // 479+1 = 480 → exactly-one-frame tick
		4321,
	};
	constexpr uint32_t N_TICKS = sizeof(ticks) / sizeof(ticks[0]);

	uint32_t kernel_frames[N_TICKS] = {};
	uint32_t kernel_state[1] = { 0u };
	dispatch_kernel(ticks, kernel_frames, N_TICKS, FRAME_SIZE, kernel_state);

	uint32_t ref_frames[N_TICKS] = {};
	uint32_t ref_carry = 0u;
	reference_policy(ticks, N_TICKS, FRAME_SIZE, ref_carry, ref_frames);

	int fails = 0;
	for (uint32_t i = 0; i < N_TICKS; ++i) {
		if (kernel_frames[i] != ref_frames[i]) {
			if (fails < 5) {
				std::fprintf(stderr,
						"framing_cursor (kernel vs ref) mismatch at i=%u: "
						"got %u, expected %u (newSamples=%u)\n",
						i, kernel_frames[i], ref_frames[i], ticks[i]);
			}
			++fails;
		}
	}
	if (kernel_state[0] != ref_carry) {
		std::fprintf(stderr,
				"framing_cursor end-carry mismatch: kernel %u, ref %u\n",
				kernel_state[0], ref_carry);
		++fails;
	}

	// ---- Job 2: split dispatch — first 7 ticks, then remainder.
	constexpr uint32_t SPLIT = 7u;
	uint32_t split_frames[N_TICKS] = {};
	uint32_t split_state[1] = { 0u };
	dispatch_kernel(ticks, split_frames, SPLIT, FRAME_SIZE, split_state);
	dispatch_kernel(ticks + SPLIT, split_frames + SPLIT, N_TICKS - SPLIT, FRAME_SIZE, split_state);

	for (uint32_t i = 0; i < N_TICKS; ++i) {
		if (split_frames[i] != kernel_frames[i]) {
			if (fails < 5) {
				std::fprintf(stderr,
						"framing_cursor (split vs one-shot) mismatch at i=%u: "
						"got %u, expected %u\n",
						i, split_frames[i], kernel_frames[i]);
			}
			++fails;
		}
	}
	if (split_state[0] != kernel_state[0]) {
		std::fprintf(stderr,
				"framing_cursor split-end-carry mismatch: split %u, one-shot %u\n",
				split_state[0], kernel_state[0]);
		++fails;
	}

	// ---- Job 3: demonstrate the bug in the speech_processor.cpp:130 loop.
	//
	// Two divergence cases:
	//   (a) Underflow:  resampled_frame_count < frameSize ⇒ buggy loop
	//                   thinks ~4 G frames are available, runs to fuse.
	//   (b) Off-by-one: resampled_frame_count == frameSize ⇒ buggy loop
	//                   emits 0, kernel correctly emits 1.

	struct Case {
		uint32_t prior_carry;
		uint32_t newSamples;
		uint32_t expected_kernel_frames;
		const char *label;
	};
	const Case cases[] = {
		// (a) underflow: prior_carry = 0, newSamples = 100 (< 480)
		{ 0u, 100u, 0u, "underflow: 0+100" },
		// (a) underflow: prior_carry = 50, newSamples = 0 (resampler returned 0)
		{ 50u, 0u, 0u, "underflow: 50+0" },
		// (b) off-by-one: total == frameSize exactly
		{ 0u, FRAME_SIZE, 1u, "off-by-one: 0+480" },
		{ 240u, FRAME_SIZE - 240u, 1u, "off-by-one: 240+240" },
		// happy path (both should agree)
		{ 0u, 2 * FRAME_SIZE, 2u, "happy: 0+960" },
	};

	int diff_found = 0;
	std::printf("framing_cursor diff against speech_processor.cpp:130 loop:\n");
	for (const auto &c : cases) {
		// Kernel result
		uint32_t k_frames[1] = { 0u };
		uint32_t k_state[1] = { c.prior_carry };
		dispatch_kernel(&c.newSamples, k_frames, 1, FRAME_SIZE, k_state);

		// Buggy C++ result
		BuggyResult b = buggy_speech_processor_cpp_loop(
				c.prior_carry, c.newSamples, FRAME_SIZE);

		const bool agree = (k_frames[0] == b.framesEmitted) && !b.hit_fuse;
		std::printf("  %-28s kernel=%u  buggy=%u%s  expected=%u  %s\n",
				c.label,
				k_frames[0], b.framesEmitted,
				b.hit_fuse ? "(FUSE-CAPPED)" : "",
				c.expected_kernel_frames,
				agree ? "AGREE" : "DIFF");

		if (k_frames[0] != c.expected_kernel_frames) {
			std::fprintf(stderr,
					"framing_cursor kernel disagrees with hand-checked "
					"oracle on '%s': got %u, expected %u\n",
					c.label, k_frames[0], c.expected_kernel_frames);
			++fails;
		}
		if (!agree)
			++diff_found;
	}

	// We *require* at least one diff to demonstrate the bug. If both
	// agree everywhere, either the bug is gone (great — note it!) or
	// the harness regressed (bad — fix it).
	if (diff_found == 0) {
		std::fprintf(stderr,
				"framing_cursor: expected at least one DIFF case to demonstrate "
				"the speech_processor.cpp:130 underflow/off-by-one bug — got 0. "
				"Either the bug was fixed (update this comment + the decision doc) "
				"or the harness regressed.\n");
		++fails;
	}

	const auto t1 = std::chrono::steady_clock::now();
	const double elapsed = std::chrono::duration<double>(t1 - t0).count();
	if (elapsed > kBudgetSeconds) {
		std::fprintf(stderr, "framing_cursor: TIMEOUT — %.3fs > %.1fs\n",
				elapsed, kBudgetSeconds);
		return 2;
	}
	if (fails == 0) {
		std::printf("framing_cursor: %u ticks OK + %d bug-demo diffs vs "
					"speech_processor.cpp:130 (%.1fms)\n",
				N_TICKS, diff_found, elapsed * 1000.0);
		return 0;
	}
	std::fprintf(stderr, "framing_cursor: %d FAIL\n", fails);
	return 1;
}
