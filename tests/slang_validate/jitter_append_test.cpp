// Real-input validator for Speech.SlangCodegen.JitterAppend.
//
// Reference: lean/Speech/SlangCodegen/JitterAppend.lean. Encodes the
// per-packet jitter-buffer evolution policy that runs inside
// Speech::on_received_audio_packet (speech.cpp:570+).
//
// Cases covered by the fixture:
//   0: first packet (currentSeqValid = 0 → forward-by-1 path)
//   1: in-order next packet
//   2: gap of 2 (skipped one sequence ID)
//   3: large gap that overflows maxSize → dropFromFront triggers
//   4: duplicate (offset == 0)
//   5: out-of-order, in-range (slot exists)
//   6: out-of-order, too old (slot < 0)
//
// Each case is checked against (a) an independent CPU reference
// implementation that mirrors the Lean spec, and (b) a hand-checked
// oracle of the expected output tuple.

#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstring>

#include "jitter_append_emit.cpp"

static constexpr double kBudgetSeconds = 5.0;

struct State {
	uint32_t currentSeq;
	uint32_t currentSeqValid;
	uint32_t currentSize;
};

struct EventOut {
	uint32_t fillerCount;
	uint32_t appendNew;
	uint32_t slotIsValid;
	uint32_t slotIdx;
	uint32_t dropFromFront;
};

// CPU reference, kept literally identical to the Lean spec.
static void reference_policy(
		const uint32_t *incomingSeq, uint32_t numEvents, uint32_t maxSize,
		State &st, EventOut *out) {
	for (uint32_t i = 0; i < numEvents; ++i) {
		const uint32_t seq = incomingSeq[i];
		if (st.currentSeqValid == 0u) {
			out[i].fillerCount = 0u;
			out[i].appendNew = 1u;
			out[i].slotIsValid = 0u;
			out[i].slotIdx = 0u;
			const uint32_t sizeAfter = st.currentSize + 1u;
			const uint32_t drop = (sizeAfter > maxSize) ? (sizeAfter - maxSize) : 0u;
			out[i].dropFromFront = drop;
			st.currentSize = sizeAfter - drop;
			st.currentSeq = seq;
			st.currentSeqValid = 1u;
		} else if (seq > st.currentSeq) {
			const uint32_t offset = seq - st.currentSeq;
			const uint32_t filler = offset - 1u;
			out[i].fillerCount = filler;
			out[i].appendNew = 1u;
			out[i].slotIsValid = 0u;
			out[i].slotIdx = 0u;
			const uint32_t sizeAfter = st.currentSize + filler + 1u;
			const uint32_t drop = (sizeAfter > maxSize) ? (sizeAfter - maxSize) : 0u;
			out[i].dropFromFront = drop;
			st.currentSize = sizeAfter - drop;
			st.currentSeq = seq;
		} else {
			const uint32_t diff = st.currentSeq - seq;
			out[i].fillerCount = 0u;
			out[i].appendNew = 0u;
			out[i].dropFromFront = 0u;
			if (st.currentSize >= 1u + diff) {
				out[i].slotIsValid = 1u;
				out[i].slotIdx = st.currentSize - 1u - diff;
			} else {
				out[i].slotIsValid = 0u;
				out[i].slotIdx = 0u;
			}
			// currentSeq / currentSize unchanged
		}
	}
}

static void dispatch_kernel(
		const uint32_t *incomingSeq, uint32_t numEvents, uint32_t maxSize,
		State &st, EventOut *out) {
	JitterAppendParams_0 params{ numEvents, maxSize };

	uint32_t fillerCount[64] = {};
	uint32_t appendNew[64] = {};
	uint32_t slotIsValid[64] = {};
	uint32_t slotIdx[64] = {};
	uint32_t dropFromFront[64] = {};
	uint32_t stateIO[3] = { st.currentSeq, st.currentSeqValid, st.currentSize };

	if (numEvents > 64) {
		std::fprintf(stderr, "fixture too large for static buffer\n");
		std::abort();
	}

	GlobalParams_0 gp{};
	gp.params_0 = &params;
	gp.incomingSeq_0.data = const_cast<uint32_t *>(incomingSeq);
	gp.incomingSeq_0.count = numEvents;
	gp.fillerCount_0.data = fillerCount;
	gp.fillerCount_0.count = numEvents;
	gp.appendNew_0.data = appendNew;
	gp.appendNew_0.count = numEvents;
	gp.slotIsValid_0.data = slotIsValid;
	gp.slotIsValid_0.count = numEvents;
	gp.slotIdx_0.data = slotIdx;
	gp.slotIdx_0.count = numEvents;
	gp.dropFromFront_0.data = dropFromFront;
	gp.dropFromFront_0.count = numEvents;
	gp.stateIO_0.data = stateIO;
	gp.stateIO_0.count = 3;

	ComputeVaryingInput vi{};
	vi.startGroupID = uint3(0, 0, 0);
	vi.endGroupID = uint3(1, 1, 1);
	main_0(&vi, nullptr, &gp);

	for (uint32_t i = 0; i < numEvents; ++i) {
		out[i].fillerCount = fillerCount[i];
		out[i].appendNew = appendNew[i];
		out[i].slotIsValid = slotIsValid[i];
		out[i].slotIdx = slotIdx[i];
		out[i].dropFromFront = dropFromFront[i];
	}
	st.currentSeq = stateIO[0];
	st.currentSeqValid = stateIO[1];
	st.currentSize = stateIO[2];
}

static bool event_equal(const EventOut &a, const EventOut &b) {
	if (a.fillerCount != b.fillerCount)
		return false;
	if (a.appendNew != b.appendNew)
		return false;
	if (a.slotIsValid != b.slotIsValid)
		return false;
	if (a.dropFromFront != b.dropFromFront)
		return false;
	// slotIdx only meaningful when slotIsValid == 1
	if (a.slotIsValid == 1u && a.slotIdx != b.slotIdx)
		return false;
	return true;
}

int main() {
	const auto t0 = std::chrono::steady_clock::now();

	constexpr uint32_t MAX_SIZE = 16u;

	// Fixture sequence designed to walk every branch.
	//
	//  i  seq  expected behavior
	//  0   42  first-packet → fillerCount=0, appendNew=1, size: 0→1
	//  1   43  in-order → filler=0, append=1, size: 1→2
	//  2   45  gap of 2 → filler=1, append=1, size: 2→4
	//  3  100  large gap (offset=55) → filler=54, append=1, size: 4+54+1=59, drop=43, newSize=16
	//  4  100  duplicate → filler=0, append=0, slotIsValid=1 (size>=1), slotIdx=size-1=15
	//  5   95  out-of-order (offset=-5, diff=5) → slotIsValid=1 (size=16 >= 1+5), slotIdx=15-5=10
	//  6    1  out-of-order, too old (diff=99) → slotIsValid=0
	//  7  101  forward by 1 → filler=0, append=1, sizeAfter=17, drop=1, newSize=16

	const uint32_t seqs[] = { 42u, 43u, 45u, 100u, 100u, 95u, 1u, 101u };
	constexpr uint32_t N = sizeof(seqs) / sizeof(seqs[0]);

	// Hand-checked oracle (slotIdx only checked when slotIsValid==1).
	const EventOut oracle[N] = {
		// fillerCount, appendNew, slotIsValid, slotIdx, dropFromFront
		{ 0u, 1u, 0u, 0u, 0u }, // i=0
		{ 0u, 1u, 0u, 0u, 0u }, // i=1
		{ 1u, 1u, 0u, 0u, 0u }, // i=2
		{ 54u, 1u, 0u, 0u, 43u }, // i=3
		{ 0u, 0u, 1u, 15u, 0u }, // i=4
		{ 0u, 0u, 1u, 10u, 0u }, // i=5
		{ 0u, 0u, 0u, 0u, 0u }, // i=6
		{ 0u, 1u, 0u, 0u, 1u }, // i=7
	};

	// Expected final state after all 8 events.
	const State oracle_end = { /*currentSeq*/ 101u, /*valid*/ 1u, /*currentSize*/ 16u };

	int fails = 0;

	// ---- Pass 1: kernel one-shot.
	State k_st = { 0u, 0u, 0u };
	EventOut k_out[N];
	dispatch_kernel(seqs, N, MAX_SIZE, k_st, k_out);

	// ---- Pass 2: CPU reference.
	State r_st = { 0u, 0u, 0u };
	EventOut r_out[N];
	reference_policy(seqs, N, MAX_SIZE, r_st, r_out);

	// Kernel vs reference (bit-exact).
	for (uint32_t i = 0; i < N; ++i) {
		if (!event_equal(k_out[i], r_out[i])) {
			if (fails < 5) {
				std::fprintf(stderr,
						"jitter_append (kernel vs ref) mismatch at i=%u seq=%u: "
						"kernel {fc=%u,a=%u,sv=%u,si=%u,df=%u} "
						"ref {fc=%u,a=%u,sv=%u,si=%u,df=%u}\n",
						i, seqs[i],
						k_out[i].fillerCount, k_out[i].appendNew, k_out[i].slotIsValid,
						k_out[i].slotIdx, k_out[i].dropFromFront,
						r_out[i].fillerCount, r_out[i].appendNew, r_out[i].slotIsValid,
						r_out[i].slotIdx, r_out[i].dropFromFront);
			}
			++fails;
		}
	}

	// Kernel vs oracle.
	for (uint32_t i = 0; i < N; ++i) {
		if (!event_equal(k_out[i], oracle[i])) {
			if (fails < 5) {
				std::fprintf(stderr,
						"jitter_append (kernel vs oracle) mismatch at i=%u seq=%u: "
						"kernel {fc=%u,a=%u,sv=%u,si=%u,df=%u} "
						"oracle {fc=%u,a=%u,sv=%u,si=%u,df=%u}\n",
						i, seqs[i],
						k_out[i].fillerCount, k_out[i].appendNew, k_out[i].slotIsValid,
						k_out[i].slotIdx, k_out[i].dropFromFront,
						oracle[i].fillerCount, oracle[i].appendNew, oracle[i].slotIsValid,
						oracle[i].slotIdx, oracle[i].dropFromFront);
			}
			++fails;
		}
	}

	// Final state check.
	if (k_st.currentSeq != oracle_end.currentSeq ||
			k_st.currentSeqValid != oracle_end.currentSeqValid ||
			k_st.currentSize != oracle_end.currentSize) {
		std::fprintf(stderr,
				"jitter_append final state mismatch: kernel {seq=%u,valid=%u,size=%u} "
				"oracle {seq=%u,valid=%u,size=%u}\n",
				k_st.currentSeq, k_st.currentSeqValid, k_st.currentSize,
				oracle_end.currentSeq, oracle_end.currentSeqValid, oracle_end.currentSize);
		++fails;
	}

	// ---- Pass 3: split dispatch — first 4 events, then remainder.
	State sp_st = { 0u, 0u, 0u };
	EventOut sp_out[N];
	constexpr uint32_t SPLIT = 4u;
	dispatch_kernel(seqs, SPLIT, MAX_SIZE, sp_st, sp_out);
	dispatch_kernel(seqs + SPLIT, N - SPLIT, MAX_SIZE, sp_st, sp_out + SPLIT);

	for (uint32_t i = 0; i < N; ++i) {
		if (!event_equal(sp_out[i], k_out[i])) {
			if (fails < 5) {
				std::fprintf(stderr,
						"jitter_append (split vs one-shot) mismatch at i=%u\n", i);
			}
			++fails;
		}
	}
	if (sp_st.currentSeq != k_st.currentSeq ||
			sp_st.currentSeqValid != k_st.currentSeqValid ||
			sp_st.currentSize != k_st.currentSize) {
		std::fprintf(stderr, "jitter_append split-end-state mismatch\n");
		++fails;
	}

	const auto t1 = std::chrono::steady_clock::now();
	const double elapsed = std::chrono::duration<double>(t1 - t0).count();
	if (elapsed > kBudgetSeconds) {
		std::fprintf(stderr, "jitter_append: TIMEOUT — %.3fs > %.1fs\n",
				elapsed, kBudgetSeconds);
		return 2;
	}
	if (fails == 0) {
		std::printf("jitter_append: %u/%u events OK (kernel vs ref/oracle/split + final state; %.1fms)\n",
				N, N, elapsed * 1000.0);
		return 0;
	}
	std::fprintf(stderr, "jitter_append: %d FAIL\n", fails);
	return 1;
}
