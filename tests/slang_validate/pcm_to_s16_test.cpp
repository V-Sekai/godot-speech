// Real-input validator for Speech.SlangCodegen.PcmToS16.
//
// Reference: lean/Speech/SlangCodegen/PcmToS16.lean. Mirrors the
// inner loop of SpeechProcessor::_mix_audio (speech_processor.cpp):
//
//   scaled  = sample * 32767.0f
//   clamped = clamp(scaled, -32768.0f, 32767.0f)
//   out[i]  = (int)clamped
//
// The CPU reference uses the same formula, so the comparison is
// bit-exact: clamp + cast are deterministic under IEEE 754
// round-toward-zero for the integer conversion. The fixture covers
// the encode-side edge cases: zero, positive / negative mid-range,
// the canonical 1.0 / -1.0 endpoints, and over-range inputs that
// must clamp.

#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>

#include "pcm_to_s16_emit.cpp"

static constexpr double kBudgetSeconds = 5.0;

static std::int32_t ref_encode(float v) {
	const float scaled = v * 32767.0f;
	const float clamped = std::fmin(32767.0f, std::fmax(-32768.0f, scaled));
	return static_cast<std::int32_t>(clamped);
}

int main() {
	const auto t0 = std::chrono::steady_clock::now();

	const float in[] = {
		0.0f, // -> 0
		0.5f, // -> 16383 (truncation toward zero from 16383.5)
		-0.5f, // -> -16383
		1.0f, // -> 32767 (max int16)
		-1.0f, // -> -32767 (NOT -32768 — encoder can't reach it)
		1.5f, // -> 32767 (clamp upper)
		-1.5f, // -> -32768 (clamp lower)
		1.0f / 32768.0f, // -> 0 (1/32768 * 32767 ≈ 0.99997 → trunc to 0)
		2.0f / 32768.0f, // -> 1 (2/32768 * 32767 ≈ 1.99994 → trunc to 1)
	};
	constexpr std::uint32_t N = sizeof(in) / sizeof(in[0]);
	constexpr std::uint32_t GROUP_SIZE = 64;
	static_assert(N <= GROUP_SIZE, "fixture must fit in one thread group");

	std::int32_t kernel_out[GROUP_SIZE] = {};

	PcmToS16Params_0 params{ N };
	GlobalParams_0 gp{};
	gp.params_0 = &params;
	gp.samplesIn_0.data = const_cast<float *>(in);
	gp.samplesIn_0.count = N;
	gp.samplesOut_0.data = kernel_out;
	gp.samplesOut_0.count = GROUP_SIZE;

	ComputeVaryingInput vi{};
	vi.startGroupID = uint3(0, 0, 0);
	vi.endGroupID = uint3(1, 1, 1);
	main_0(&vi, nullptr, &gp);

	int fails = 0;
	for (std::uint32_t i = 0; i < N; ++i) {
		const std::int32_t ref = ref_encode(in[i]);
		if (kernel_out[i] != ref) {
			if (fails < 5) {
				std::fprintf(stderr,
						"pcm_to_s16 mismatch at i=%u: in=%g got=%d ref=%d\n",
						i, in[i], kernel_out[i], ref);
			}
			++fails;
		}
	}

	const auto t1 = std::chrono::steady_clock::now();
	const double elapsed = std::chrono::duration<double>(t1 - t0).count();
	if (elapsed > kBudgetSeconds) {
		std::fprintf(stderr, "pcm_to_s16: TIMEOUT — %.3fs > %.1fs\n",
				elapsed, kBudgetSeconds);
		return 2;
	}
	if (fails == 0) {
		std::printf("pcm_to_s16: %u/%u samples OK (encode + clamp; %.1fms)\n",
				N, N, elapsed * 1000.0);
		return 0;
	}
	std::fprintf(stderr, "pcm_to_s16: %d FAIL\n", fails);
	return 1;
}
