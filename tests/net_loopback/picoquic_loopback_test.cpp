// CHI-101 Phase A — net_loopback step 4b: real picoquic datagram loopback.
//
// Sanity-check driver: stand up a real two-context UDP loopback over
// QUIC and confirm 32 datagrams round-trip byte-exact. The heavy
// lifting lives in `picoquic_loopback_core.h`; this binary fixes the
// parameters at (count=32, size=64) so it stays fast in CI.
//
// Wider count + payload sweeps live in `picoquic_stress_test.cpp`.

#include "picoquic_loopback_core.h"

#include <cstdio>

int main() {
	using namespace picoquic_loopback;

	std::printf("picoquic_loopback: starting (picoquic %s)\n", PICOQUIC_VERSION);

	LoopbackConfig cfg;
	cfg.datagram_count = 32;
	cfg.datagram_size = 64;

	const LoopbackResult r = run_picoquic_loopback(cfg);

	std::printf("picoquic_loopback: queued=%d acked=%d received=%d connections=%d elapsed=%.1f ms\n",
			r.datagrams_queued, r.datagrams_acked, r.datagrams_received,
			r.server_connections_seen, r.elapsed_ms);

	int fails = 0;
	if (r.timed_out) {
		std::fprintf(stderr, "FAIL: client deadline exceeded\n");
		++fails;
	}
	if (r.datagrams_queued != cfg.datagram_count) {
		std::fprintf(stderr, "FAIL: queued %d (expected %d)\n", r.datagrams_queued, cfg.datagram_count);
		++fails;
	}
	if (r.datagrams_received != cfg.datagram_count) {
		std::fprintf(stderr, "FAIL: received %d (expected %d)\n", r.datagrams_received, cfg.datagram_count);
		++fails;
	}
	if (r.server_connections_seen == 0) {
		std::fprintf(stderr, "FAIL: server never reached picoquic_callback_ready\n");
		++fails;
	}
	if (r.payload_mismatches != 0) {
		std::fprintf(stderr, "FAIL: %d payload mismatches\n", r.payload_mismatches);
		++fails;
	}
	if (r.missing_sequences != 0) {
		std::fprintf(stderr, "FAIL: %d missing sequences\n", r.missing_sequences);
		++fails;
	}

	if (fails == 0) {
		std::printf("picoquic_loopback: PASS — %d datagrams round-tripped over real QUIC\n",
				cfg.datagram_count);
		return 0;
	}
	std::fprintf(stderr, "picoquic_loopback: %d FAIL\n", fails);
	return 1;
}
