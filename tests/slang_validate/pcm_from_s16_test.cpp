// Real-input validator for Speech.SlangCodegen.PcmFromS16.
//
// Reference: lean/Speech/SlangCodegen/PcmFromS16.lean. Mirrors
// SpeechProcessor::_16_pcm_mono_to_real_stereo (speech_processor.cpp):
//
//   out[i] = (float)s16[i] / 32768.0f
//
// Bit-exact: dividing by 32768 (a power of two) is an exact
// binary-exponent shift in IEEE 754, and `(float)(int32_t)` is
// deterministic for values within the s16 range. Round-trip 1 ULP
// concerns belong to pcm_roundtrip_test, not here.

#include <chrono>
#include <cstdint>
#include <cstdio>

#include "pcm_from_s16_emit.cpp"

static constexpr double kBudgetSeconds = 5.0;

static float ref_decode(std::int32_t s) {
	return static_cast<float>(s) / 32768.0f;
}

int main() {
	const auto t0 = std::chrono::steady_clock::now();

	const std::int32_t in[] = {
		0, 1, -1, 16384, -16384, 32767, -32767, -32768, 12345
	};
	constexpr std::uint32_t N = sizeof(in) / sizeof(in[0]);
	constexpr std::uint32_t GROUP_SIZE = 64;
	static_assert(N <= GROUP_SIZE, "fixture must fit in one thread group");

	float kernel_out[GROUP_SIZE] = {};

	PcmFromS16Params_0 params{ N };
	GlobalParams_0 gp{};
	gp.params_0 = &params;
	gp.samplesIn_0.data = const_cast<std::int32_t *>(in);
	gp.samplesIn_0.count = N;
	gp.samplesOut_0.data = kernel_out;
	gp.samplesOut_0.count = GROUP_SIZE;

	ComputeVaryingInput vi{};
	vi.startGroupID = uint3(0, 0, 0);
	vi.endGroupID = uint3(1, 1, 1);
	main_0(&vi, nullptr, &gp);

	int fails = 0;
	for (std::uint32_t i = 0; i < N; ++i) {
		const float ref = ref_decode(in[i]);
		if (kernel_out[i] != ref) {
			if (fails < 5) {
				std::fprintf(stderr,
						"pcm_from_s16 mismatch at i=%u: in=%d got=%g ref=%g\n",
						i, in[i], kernel_out[i], ref);
			}
			++fails;
		}
	}

	const auto t1 = std::chrono::steady_clock::now();
	const double elapsed = std::chrono::duration<double>(t1 - t0).count();
	if (elapsed > kBudgetSeconds) {
		std::fprintf(stderr, "pcm_from_s16: TIMEOUT — %.3fs > %.1fs\n",
				elapsed, kBudgetSeconds);
		return 2;
	}
	if (fails == 0) {
		std::printf("pcm_from_s16: %u/%u samples OK (decode; %.1fms)\n",
				N, N, elapsed * 1000.0);
		return 0;
	}
	std::fprintf(stderr, "pcm_from_s16: %d FAIL\n", fails);
	return 1;
}
