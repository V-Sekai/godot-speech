// CHI-101 Phase A — net_loopback step 4c: picoquic datagram stress sweep.
//
// Drives the same client / server / packet-loop harness as
// `picoquic_loopback_test.cpp` but sweeps several (count, payload
// size) configurations to exercise:
//
//   * High datagram counts (queue management, ack pacing).
//   * Large payloads up to ~MTU (transport-layer fragmentation).
//   * The combination of both (sustained throughput).
//
// QUIC datagrams are unreliable by design — like raw UDP they can
// be dropped at the kernel socket buffer or by picoquic if a frame
// won't fit in the current send MTU. The stress sweep asserts:
//
//   * No payload corruption: every received datagram matches
//     `fill_payload` byte-for-byte. (Hard requirement.)
//   * Loss rate < 50% per config. Loopback over localhost should be
//     near-perfect on small configs; bursty large-payload configs
//     may shed packets at the UDP recv buffer. This bound is a
//     regression guard, not a performance promise.
//   * Server sees the connection establish (1+ ready callbacks).

#include "picoquic_loopback_core.h"

#include <cstdio>
#include <vector>

namespace {
struct StressRow {
	const char *label;
	int count;
	size_t size;
	int max_missing; // hard-fail threshold per config
};
} // namespace

int main() {
	using namespace picoquic_loopback;

	std::printf("picoquic_stress: starting (picoquic %s)\n", PICOQUIC_VERSION);

	// Per-config max_missing keeps the test deterministic-enough on
	// CI runners while still flagging real regressions (e.g. handshake
	// failure shows up as "0 received").
	const std::vector<StressRow> rows = {
		// Small bursts: byte-exact, no loss expected.
		{ "baseline   ", 32, 64, 0 },
		// High count with small payloads: sustained pacing test.
		{ "high-count ", 256, 64, 0 },
		// Mid-size payloads: under-MTU comfortably.
		{ "mid-size   ", 128, 512, 0 },
		// Near-MTU payloads (~800 + 21 byte frame overhead + 8 byte
		// cnxid = 829 of 1252 MTU). One deterministic drop on macOS
		// at seq 5 (picoquic PMTU probe artifact); cap allowance at 2.
		{ "near-mtu   ", 64, 800, 2 },
		// Very high count: 1024 datagrams, exercises queue growth +
		// ack pipelining over the connection.
		{ "very-high  ", 1024, 64, 0 },
	};

	int total_fails = 0;
	std::printf("%-12s | %5s | %5s | %7s %7s %7s | %7s | %8s | %8s\n",
			"label", "count", "size", "queued", "acked", "rcvd", "missing", "mismatch", "ms");
	std::printf("-------------+-------+-------+-------------------------+---------+----------+---------\n");

	for (const auto &row : rows) {
		LoopbackConfig cfg;
		cfg.datagram_count = row.count;
		cfg.datagram_size = row.size;

		const LoopbackResult r = run_picoquic_loopback(cfg);

		int fails = 0;
		if (r.datagrams_queued != row.count) {
			++fails;
		}
		if (r.server_connections_seen == 0) {
			++fails;
		}
		if (r.payload_mismatches != 0) {
			++fails;
		}
		if (r.missing_sequences > row.max_missing) {
			++fails;
		}

		std::printf("%-12s | %5d | %5zu | %7d %7d %7d | %7d | %8d | %8.1f%s\n",
				row.label, row.count, row.size,
				r.datagrams_queued, r.datagrams_acked, r.datagrams_received,
				r.missing_sequences, r.payload_mismatches,
				r.elapsed_ms,
				(fails == 0) ? "  OK" : "  FAIL");
		total_fails += fails;
	}

	if (total_fails == 0) {
		std::printf("\npicoquic_stress: PASS — all %zu configurations within loss bounds\n", rows.size());
		return 0;
	}
	std::fprintf(stderr, "\npicoquic_stress: %d FAIL across %zu configurations\n",
			total_fails, rows.size());
	return 1;
}
