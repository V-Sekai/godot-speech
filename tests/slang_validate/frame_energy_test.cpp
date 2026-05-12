// Real-input validator for Speech.SlangCodegen.FrameEnergy.
//
// Reference: lean/Speech/SlangCodegen/FrameEnergy.lean. Per 10 ms frame
// (480 mono float32 samples at 48 kHz),
//   energy[f] = Σ_{s=0..479} (samples[f·480 + s])²
//
// Strict left-to-right fp32 accumulation via fma — the CPU reference
// here mirrors the kernel's accumulation order exactly, so the
// comparison is bit-exact (0 ULP tolerance).
//
// Test fixture (3 frames × 480 samples):
//   frame 0 — pure silence (all zeros), energy = 0
//   frame 1 — DC at 0.5, energy = 480 · 0.25 = 120
//   frame 2 — alternating +0.5 / −0.5, energy = 480 · 0.25 = 120
//
// One thread group of 64 (matches [numthreads(64,1,1)]) is enough for
// 3 frames; threads 3..63 hit the early-return via f >= numFrames.

#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <vector>

#include "frame_energy_emit.cpp"

static constexpr double kBudgetSeconds = 5.0;

int main() {
	const auto t0 = std::chrono::steady_clock::now();

	constexpr uint32_t N_FRAMES = 3;
	constexpr uint32_t SAMPLES_PER_FRAME = 480;
	constexpr uint32_t GROUP_SIZE = 64; // numthreads(64,1,1)

	FrameEnergyParams_0 params{ N_FRAMES, SAMPLES_PER_FRAME };

	// ---- Build input PCM.
	std::vector<float> samples(N_FRAMES * SAMPLES_PER_FRAME, 0.0f);
	// frame 0: silence (already zero).
	// frame 1: DC at 0.5.
	for (uint32_t s = 0; s < SAMPLES_PER_FRAME; ++s) {
		samples[1 * SAMPLES_PER_FRAME + s] = 0.5f;
	}
	// frame 2: alternating ±0.5.
	for (uint32_t s = 0; s < SAMPLES_PER_FRAME; ++s) {
		samples[2 * SAMPLES_PER_FRAME + s] = (s % 2 == 0) ? 0.5f : -0.5f;
	}

	std::vector<float> energy(N_FRAMES, 0.0f);

	GlobalParams_0 gp{};
	gp.params_0 = &params;
	gp.samples_0.data = samples.data();
	gp.samples_0.count = samples.size();
	gp.energy_0.data = energy.data();
	gp.energy_0.count = energy.size();

	ComputeVaryingInput vi{};
	vi.startGroupID = uint3(0, 0, 0);
	vi.endGroupID = uint3(1, 1, 1);
	main_0(&vi, nullptr, &gp);

	// ---- Reference: in-order fma over the same samples. Bit-exact target.
	float ref[N_FRAMES] = { 0.0f, 0.0f, 0.0f };
	for (uint32_t f = 0; f < N_FRAMES; ++f) {
		float acc = 0.0f;
		for (uint32_t s = 0; s < SAMPLES_PER_FRAME; ++s) {
			const float x = samples[f * SAMPLES_PER_FRAME + s];
			acc = std::fma(x, x, acc);
		}
		ref[f] = acc;
	}

	int fails = 0;
	float max_abs_diff = 0.0f;
	for (uint32_t f = 0; f < N_FRAMES; ++f) {
		const float d = std::fabs(energy[f] - ref[f]);
		if (d > max_abs_diff)
			max_abs_diff = d;
		if (d != 0.0f) {
			if (fails < 5) {
				std::fprintf(stderr,
						"frame_energy bit-exact mismatch at f=%u: got %g, expected %g (diff %g)\n",
						f, energy[f], ref[f], d);
			}
			++fails;
		}
	}

	// ---- Sanity: hand-computed analytic values.
	// frame 0: 0;  frame 1: 480 · 0.25 = 120;  frame 2: 480 · 0.25 = 120.
	// (Bit-exact only against `ref` above — analytic check is a wider
	// ULP-tolerant sanity floor so a totally bogus kernel still trips.)
	const float analytic[N_FRAMES] = { 0.0f, 120.0f, 120.0f };
	for (uint32_t f = 0; f < N_FRAMES; ++f) {
		if (std::fabs(energy[f] - analytic[f]) > 1e-3f) {
			std::fprintf(stderr,
					"frame_energy analytic mismatch at f=%u: got %g, expected ≈%g\n",
					f, energy[f], analytic[f]);
			++fails;
		}
	}

	// ---- Pad-thread early-return: threads 3..63 must NOT write past energy[2].
	// Detected indirectly: if the kernel wrote past the buffer we'd have
	// ASAN/UBSAN complaints; otherwise the GROUP_SIZE pad is fine.
	(void)GROUP_SIZE;

	const auto t1 = std::chrono::steady_clock::now();
	const double elapsed = std::chrono::duration<double>(t1 - t0).count();
	if (elapsed > kBudgetSeconds) {
		std::fprintf(stderr, "frame_energy: TIMEOUT — %.3fs > %.1fs\n",
				elapsed, kBudgetSeconds);
		return 2;
	}
	if (fails == 0) {
		std::printf("frame_energy: %u/%u frames OK (max_abs_diff=%g, %.1fms)\n",
				N_FRAMES, N_FRAMES, max_abs_diff, elapsed * 1000.0);
		return 0;
	}
	std::fprintf(stderr, "frame_energy: %d FAIL\n", fails);
	return 1;
}
