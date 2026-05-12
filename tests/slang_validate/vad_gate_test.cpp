// Real-input validator for Speech.SlangCodegen.VadGate.
//
// Reference: lean/Speech/SlangCodegen/VadGate.lean. Per-frame
// hysteresis state machine that turns a classifier score stream into
// a gated boolean. Inherently sequential — kernel dispatch is a
// single thread group with [numthreads(1,1,1)].
//
// This harness:
//   1. Runs a synthetic 20-frame score sequence through the kernel
//      in one shot, and through an independent CPU re-implementation
//      of the same policy. Bit-exact comparison (boolean).
//   2. Splits the same sequence across two dispatches (frames [0..9]
//      then [10..19]) and verifies the stateIO carry-over produces
//      the SAME 20-element gated output. Catches state-init or
//      carry-over bugs.
//
// Parameters: enter=0.6, exit=0.4, hangover=3. Chosen so the test
// sequence exercises every transition: enter, refresh, hangover
// countdown to zero, re-enter, exit.

#include <chrono>
#include <cstdint>
#include <cstdio>

#include "vad_gate_emit.cpp"

static constexpr double kBudgetSeconds = 5.0;

// CPU reference implementation of the policy, kept literally identical
// to the Lean spec in VadGate.lean — including the if/else nesting,
// because subtle reorderings would diverge in tie-breaks.
static void reference_policy(
    const float* score, uint32_t numFrames,
    float enterThreshold, float exitThreshold, uint32_t hangoverFrames,
    uint32_t& active, uint32_t& hangover,
    uint32_t* gated)
{
    for (uint32_t i = 0; i < numFrames; ++i) {
        const float s = score[i];
        if (active == 0u) {
            if (s > enterThreshold) {
                active   = 1u;
                hangover = hangoverFrames;
            }
        } else {
            if (s > exitThreshold) {
                hangover = hangoverFrames;
            } else if (hangover > 0u) {
                hangover = hangover - 1u;
            } else {
                active = 0u;
            }
        }
        gated[i] = active;
    }
}

// Run the kernel over [first, first+count) frames, carrying state via stateIO.
static void dispatch_kernel(
    const float* score, uint32_t* gated,
    uint32_t count,
    const VadGateParams_0& params,
    uint32_t* stateIO)
{
    GlobalParams_0 gp{};
    gp.params_0       = const_cast<VadGateParams_0*>(&params);
    gp.score_0.data   = const_cast<float*>(score);  gp.score_0.count   = count;
    gp.gated_0.data   = gated;                      gp.gated_0.count   = count;
    gp.stateIO_0.data = stateIO;                    gp.stateIO_0.count = 2;

    ComputeVaryingInput vi{};
    vi.startGroupID = uint3(0, 0, 0);
    vi.endGroupID   = uint3(1, 1, 1);
    main_0(&vi, nullptr, &gp);
}

int main() {
    const auto t0 = std::chrono::steady_clock::now();

    constexpr uint32_t N = 20;
    constexpr float    ENTER     = 0.6f;
    constexpr float    EXIT      = 0.4f;
    constexpr uint32_t HANGOVER  = 3u;

    // Synthetic score sequence, hand-designed to exercise every transition:
    //   i  : score   : phase
    //   0  : 0.10    : silence
    //   1  : 0.20    : silence
    //   2  : 0.70    : enter   (>enter ⇒ active=1, hangover=3)
    //   3  : 0.80    : sustain (>exit ⇒ refresh hangover=3)
    //   4  : 0.30    : dip     (≤exit, hangover 3→2, still active)
    //   5  : 0.30    : dip     (hangover 2→1, still active)
    //   6  : 0.30    : dip     (hangover 1→0, still active)
    //   7  : 0.30    : dip     (hangover=0 ⇒ active=0)
    //   8  : 0.30    : silence (no enter trigger)
    //   9  : 0.50    : nudge   (not > enter=0.6 ⇒ still silent)
    //  10  : 0.90    : enter   (>enter ⇒ active=1, hangover=3)
    //  11  : 0.20    : dip     (hangover 3→2)
    //  12  : 0.70    : refresh (>exit ⇒ hangover=3 again)
    //  13  : 0.70    : sustain
    //  14  : 0.10    : dip     (hangover 3→2)
    //  15  : 0.10    : dip     (hangover 2→1)
    //  16  : 0.10    : dip     (hangover 1→0)
    //  17  : 0.10    : dip     (hangover=0 ⇒ active=0)
    //  18  : 0.10    : silence
    //  19  : 0.10    : silence
    float score[N] = {
        0.10f, 0.20f, 0.70f, 0.80f, 0.30f, 0.30f, 0.30f, 0.30f, 0.30f, 0.50f,
        0.90f, 0.20f, 0.70f, 0.70f, 0.10f, 0.10f, 0.10f, 0.10f, 0.10f, 0.10f,
    };

    // Hand-checked oracle. If the kernel and the CPU reference both
    // disagree with this, the test design itself is wrong — orthogonal
    // check against the bit-exact equality below.
    const uint32_t oracle[N] = {
        0, 0, 1, 1, 1, 1, 1, 0, 0, 0,
        1, 1, 1, 1, 1, 1, 1, 0, 0, 0,
    };

    VadGateParams_0 params{N, ENTER, EXIT, HANGOVER};

    // ---- Pass 1: one-shot dispatch over [0..N).
    uint32_t gated_one[N]   = {};
    uint32_t stateIO_one[2] = {0u, 0u};
    dispatch_kernel(score, gated_one, N, params, stateIO_one);

    // ---- Pass 2: CPU reference.
    uint32_t gated_ref[N];
    uint32_t ref_active = 0u, ref_hangover = 0u;
    reference_policy(score, N, ENTER, EXIT, HANGOVER,
                     ref_active, ref_hangover, gated_ref);

    int fails = 0;

    // Bit-exact: kernel vs reference.
    for (uint32_t i = 0; i < N; ++i) {
        if (gated_one[i] != gated_ref[i]) {
            if (fails < 5) {
                std::fprintf(stderr,
                    "vad_gate (one-shot vs ref) mismatch at i=%u: got %u, expected %u (score=%g)\n",
                    i, gated_one[i], gated_ref[i], score[i]);
            }
            ++fails;
        }
    }

    // Oracle: catches case where both kernel & ref share the same bug.
    for (uint32_t i = 0; i < N; ++i) {
        if (gated_one[i] != oracle[i]) {
            if (fails < 5) {
                std::fprintf(stderr,
                    "vad_gate (one-shot vs oracle) mismatch at i=%u: got %u, expected %u (score=%g)\n",
                    i, gated_one[i], oracle[i], score[i]);
            }
            ++fails;
        }
    }

    // Kernel must leave stateIO consistent — at i=19 the gate has been
    // off for 3 frames (17,18,19), so final state should be {active=0, hangover=0}.
    if (stateIO_one[0] != ref_active || stateIO_one[1] != ref_hangover) {
        std::fprintf(stderr,
            "vad_gate stateIO end-state mismatch: kernel {%u, %u}, ref {%u, %u}\n",
            stateIO_one[0], stateIO_one[1], ref_active, ref_hangover);
        ++fails;
    }

    // ---- Pass 3: split dispatch [0..10) + [10..20) with state carry-over.
    uint32_t gated_split[N]   = {};
    uint32_t stateIO_split[2] = {0u, 0u};
    VadGateParams_0 params10{10u, ENTER, EXIT, HANGOVER};
    dispatch_kernel(score,        gated_split,       10, params10, stateIO_split);
    dispatch_kernel(score + 10,   gated_split + 10,  10, params10, stateIO_split);

    for (uint32_t i = 0; i < N; ++i) {
        if (gated_split[i] != gated_one[i]) {
            if (fails < 5) {
                std::fprintf(stderr,
                    "vad_gate (split vs one-shot) mismatch at i=%u: got %u, expected %u (score=%g)\n",
                    i, gated_split[i], gated_one[i], score[i]);
            }
            ++fails;
        }
    }

    const auto t1 = std::chrono::steady_clock::now();
    const double elapsed = std::chrono::duration<double>(t1 - t0).count();
    if (elapsed > kBudgetSeconds) {
        std::fprintf(stderr, "vad_gate: TIMEOUT — %.3fs > %.1fs\n",
                     elapsed, kBudgetSeconds);
        return 2;
    }
    if (fails == 0) {
        std::printf("vad_gate: %u/%u frames OK (one-shot, ref, oracle, split, all match; %.1fms)\n",
                    N, N, elapsed * 1000.0);
        return 0;
    }
    std::fprintf(stderr, "vad_gate: %d FAIL\n", fails);
    return 1;
}
