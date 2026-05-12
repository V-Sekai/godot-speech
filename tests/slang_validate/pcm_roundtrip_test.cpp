// Round-trip composition validator for
//   Speech.SlangCodegen.PcmToS16  (encode: * 32767, clamp, cast)
//   Speech.SlangCodegen.PcmFromS16 (decode: cast, / 32768)
//
// Two slangc-cpp emits in one translation unit — both define `main_0`
// and `GlobalParams_0` at file scope, so we follow the cloth_dynamics
// `cg_demo_test.cpp` pattern: include each emit inside its own
// namespace, with SLANG_PRELUDE_EXTERN_C macros temporarily neutered
// so the kernel symbols respect the surrounding namespace.
//
// The harness:
//   1. Encodes a sweep of float inputs across [-1, 1].
//   2. Decodes the resulting s16 values back to float.
//   3. Quantifies the deviation. The asymmetric scale on the two sides
//      (32767 encode, 32768 decode) produces a known round-trip gain
//      of 32767/32768 ≈ -0.000266 dB. Recorded in the decision doc as
//      informational finding F3 — not a bug, but pinned so future
//      review can revisit the choice with a concrete number.

#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>

#include "slang-cpp-prelude.h"

namespace rt_to {
#pragma push_macro("SLANG_PRELUDE_EXTERN_C")
#pragma push_macro("SLANG_PRELUDE_EXTERN_C_START")
#pragma push_macro("SLANG_PRELUDE_EXTERN_C_END")
#undef SLANG_PRELUDE_EXTERN_C
#undef SLANG_PRELUDE_EXTERN_C_START
#undef SLANG_PRELUDE_EXTERN_C_END
#define SLANG_PRELUDE_EXTERN_C
#define SLANG_PRELUDE_EXTERN_C_START
#define SLANG_PRELUDE_EXTERN_C_END
#include "pcm_to_s16_emit.cpp"
#pragma pop_macro("SLANG_PRELUDE_EXTERN_C")
#pragma pop_macro("SLANG_PRELUDE_EXTERN_C_START")
#pragma pop_macro("SLANG_PRELUDE_EXTERN_C_END")
} // namespace rt_to

namespace rt_from {
#pragma push_macro("SLANG_PRELUDE_EXTERN_C")
#pragma push_macro("SLANG_PRELUDE_EXTERN_C_START")
#pragma push_macro("SLANG_PRELUDE_EXTERN_C_END")
#undef SLANG_PRELUDE_EXTERN_C
#undef SLANG_PRELUDE_EXTERN_C_START
#undef SLANG_PRELUDE_EXTERN_C_END
#define SLANG_PRELUDE_EXTERN_C
#define SLANG_PRELUDE_EXTERN_C_START
#define SLANG_PRELUDE_EXTERN_C_END
#include "pcm_from_s16_emit.cpp"
#pragma pop_macro("SLANG_PRELUDE_EXTERN_C")
#pragma pop_macro("SLANG_PRELUDE_EXTERN_C_START")
#pragma pop_macro("SLANG_PRELUDE_EXTERN_C_END")
} // namespace rt_from

static constexpr double kBudgetSeconds = 5.0;

int main() {
	const auto t0 = std::chrono::steady_clock::now();

	// 65 sweep points across [-1, 1] inclusive.
	constexpr std::uint32_t N = 65;
	// Two thread groups cover 128 lanes; N = 65 fits with room for the
	// numSamples early-return on the spare 63 lanes.
	float in[N];
	for (std::uint32_t i = 0; i < N; ++i) {
		in[i] = -1.0f + 2.0f * static_cast<float>(i) / (N - 1);
	}

	// Pad input/output buffers so both groups can write safely.
	float in_pad[128] = {};
	std::int32_t encoded[128] = {};
	float decoded[128] = {};
	for (std::uint32_t i = 0; i < N; ++i)
		in_pad[i] = in[i];

	// ---- Encode dispatch.
	{
		rt_to::PcmToS16Params_0 p{ N };
		rt_to::GlobalParams_0 gp{};
		gp.params_0 = &p;
		gp.samplesIn_0.data = in_pad;
		gp.samplesIn_0.count = 128;
		gp.samplesOut_0.data = encoded;
		gp.samplesOut_0.count = 128;
		ComputeVaryingInput vi{};
		vi.startGroupID = uint3(0, 0, 0);
		vi.endGroupID = uint3(2, 1, 1); // two groups of 64
		rt_to::main_0(&vi, nullptr, &gp);
	}

	// ---- Decode dispatch.
	{
		rt_from::PcmFromS16Params_0 p{ N };
		rt_from::GlobalParams_0 gp{};
		gp.params_0 = &p;
		gp.samplesIn_0.data = encoded;
		gp.samplesIn_0.count = 128;
		gp.samplesOut_0.data = decoded;
		gp.samplesOut_0.count = 128;
		ComputeVaryingInput vi{};
		vi.startGroupID = uint3(0, 0, 0);
		vi.endGroupID = uint3(2, 1, 1);
		rt_from::main_0(&vi, nullptr, &gp);
	}

	// ---- Quantify the asymmetry.
	float max_abs_err = 0.0f;
	float max_signed_err = 0.0f;
	for (std::uint32_t i = 0; i < N; ++i) {
		const float err = decoded[i] - in[i];
		if (std::fabs(err) > std::fabs(max_signed_err))
			max_signed_err = err;
		if (std::fabs(err) > max_abs_err)
			max_abs_err = std::fabs(err);
	}

	const float expected_max_at_unity = 1.0f - 32767.0f / 32768.0f;

	std::printf("pcm_roundtrip: encode/decode asymmetry sweep (N=%u):\n", N);
	std::printf("  max |err|          = %.7g  (theoretical at x=+1: %.7g)\n",
			max_abs_err, expected_max_at_unity);
	std::printf("  max signed err     = %+.7g\n", max_signed_err);
	std::printf("  round-trip gain    = %.6f (= 32767/32768; %.4f dB)\n",
			32767.0f / 32768.0f,
			20.0 * std::log10(32767.0 / 32768.0));

	int fails = 0;
	// Sanity: max error must be bounded by 1/32767 (quantization +
	// asymmetric gain). Bigger error is a bug, not the documented F3.
	if (max_abs_err > 1.0f / 32767.0f) {
		std::fprintf(stderr,
				"pcm_roundtrip: max_abs_err %.7g exceeds 1/32767 = %.7g — bug?\n",
				max_abs_err, 1.0f / 32767.0f);
		++fails;
	}

	// Also pin: round-trip of 1.0f must be exactly 32767/32768.
	const float rt_unity = decoded[N - 1]; // last sweep point = +1.0f
	const float expected_unity = 32767.0f / 32768.0f;
	if (rt_unity != expected_unity) {
		std::fprintf(stderr,
				"pcm_roundtrip: round-trip of 1.0 = %g, expected %g\n",
				rt_unity, expected_unity);
		++fails;
	}

	const auto t1 = std::chrono::steady_clock::now();
	const double elapsed = std::chrono::duration<double>(t1 - t0).count();
	if (elapsed > kBudgetSeconds) {
		std::fprintf(stderr, "pcm_roundtrip: TIMEOUT — %.3fs > %.1fs\n",
				elapsed, kBudgetSeconds);
		return 2;
	}
	if (fails == 0) {
		std::printf("pcm_roundtrip: %u sweep points OK; F3 gain pinned at %.6f (%.1fms)\n",
				N, expected_unity, elapsed * 1000.0);
		return 0;
	}
	std::fprintf(stderr, "pcm_roundtrip: %d FAIL\n", fails);
	return 1;
}
