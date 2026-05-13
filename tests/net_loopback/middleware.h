// CHI-101 Phase A — `NetTransport` interface + the synthetic
// in-process middleware that implements it.
//
// `NetTransport` is the abstraction the loopback/observer drivers
// talk to. It exposes:
//
//   * `send(packet, now_ms)` — submit a packet from the sender
//     side, optionally annotated with a synthetic-clock timestamp
//     (the QUIC variant ignores `now_ms` — it uses wall time).
//   * `drain(now_ms)`        — return every packet that has been
//     delivered to the receiver side since the last drain, in
//     delivery order. `now_ms` is the synthetic clock the
//     in-process transport uses to decide what's "ready"; the
//     QUIC variant ignores it.
//   * counters: sent / dropped / reordered / delivered.
//   * `backend_name()` — short label for logs.
//
// `InProcessNetTransport` is the original synthetic middleware
// (renamed from `NetMiddleware`): seeded `std::mt19937_64`, queue
// with per-packet deliver_at_ms, jitter / loss / reorder applied
// according to the `NetworkProfile`. Deterministic — same profile +
// same seed = same delivery sequence.
//
// The QUIC implementation lives in `picoquic_net_transport.h` and
// runs the existing `picoquic_loopback_core.h` harness as a batched
// round-trip: send() buffers; drain() runs the whole batch through
// a real client+server picoquic pair and returns the bytes the
// server actually received.

#pragma once

#include <algorithm>
#include <cstdint>
#include <deque>
#include <random>
#include <vector>

#include "core/core_types.h"

struct NetworkProfile {
	// Static one-way latency in ms — every packet waits at least
	// this long before delivery.
	double base_latency_ms = 0.0;
	// Per-packet jitter σ in ms — added on top of base_latency_ms,
	// drawn from a half-normal distribution (always >= 0).
	double jitter_sigma_ms = 0.0;
	// Drop probability ∈ [0, 1].
	double loss_prob = 0.0;
	// Reorder window: each packet may be swapped with one within
	// the next `reorder_window` ingress positions with probability
	// `reorder_prob`. Matches typical out-of-order reordering on
	// a congested WAN path.
	int reorder_window = 0;
	double reorder_prob = 0.0;
	// RNG seed for deterministic replays. Same profile + same
	// seed = same sequence of delivery decisions.
	uint64_t seed = 0xC011'101'F1u;
};

struct DeliveredPacket {
	PackedByteArray packet;
	int sequence_id = 0; // sender's view of order; preserved across drop/reorder
};

struct NetTransport {
	int sent = 0;
	int dropped = 0;
	int reordered = 0;
	int delivered = 0;

	virtual ~NetTransport() = default;
	// Submit a packet from the sender side. Returns false if the
	// transport dropped the packet at submission (in-process loss),
	// true otherwise. `now_ms` is the synthetic clock the in-process
	// transport uses to compute deliver_at_ms; real transports may
	// ignore it.
	virtual bool send(const PackedByteArray &packet, double now_ms) = 0;
	// Return every packet delivered up to `now_ms`, in delivery
	// order. Real transports may ignore `now_ms` and return whatever
	// has actually arrived.
	virtual std::vector<DeliveredPacket> drain(double now_ms) = 0;
	// Short label for logs / dashboards.
	virtual const char *backend_name() const = 0;
};

struct InProcessNetTransport : public NetTransport {
	struct InFlight {
		PackedByteArray packet;
		int sequence_id = 0;
		double deliver_at_ms = 0.0;
	};

	NetworkProfile profile;
	std::mt19937_64 rng;
	std::deque<InFlight> queue;
	int next_sequence = 1;

	explicit InProcessNetTransport(NetworkProfile p) :
			profile(p), rng(p.seed) {}

	const char *backend_name() const override { return "in-process"; }

	bool send(const PackedByteArray &packet, double now_ms) override {
		++sent;
		if (profile.loss_prob > 0.0) {
			std::uniform_real_distribution<double> u(0.0, 1.0);
			if (u(rng) < profile.loss_prob) {
				++dropped;
				return false;
			}
		}

		double delay = profile.base_latency_ms;
		if (profile.jitter_sigma_ms > 0.0) {
			std::normal_distribution<double> n(0.0, profile.jitter_sigma_ms);
			delay += std::abs(n(rng));
		}

		InFlight inflight;
		inflight.packet = packet;
		inflight.sequence_id = next_sequence++;
		inflight.deliver_at_ms = now_ms + delay;
		queue.push_back(inflight);

		// Reorder: occasionally swap the just-enqueued packet
		// with one earlier in the queue (within reorder_window).
		if (profile.reorder_window > 0 && profile.reorder_prob > 0.0 && queue.size() >= 2) {
			std::uniform_real_distribution<double> u(0.0, 1.0);
			if (u(rng) < profile.reorder_prob) {
				const int window = std::min<int>(
						static_cast<int>(queue.size()) - 1,
						profile.reorder_window);
				if (window > 0) {
					std::uniform_int_distribution<int> dpos(1, window);
					const int back_off = dpos(rng);
					const auto last = queue.end() - 1;
					std::iter_swap(last, last - back_off);
					++reordered;
				}
			}
		}
		return true;
	}

	std::vector<DeliveredPacket> drain(double now_ms) override {
		std::vector<DeliveredPacket> out;
		while (!queue.empty() && queue.front().deliver_at_ms <= now_ms) {
			DeliveredPacket d;
			d.packet = std::move(queue.front().packet);
			d.sequence_id = queue.front().sequence_id;
			out.push_back(std::move(d));
			queue.pop_front();
			++delivered;
		}
		return out;
	}
};

// Back-compat alias for the original concrete type. Existing tests
// that constructed `NetMiddleware net(profile)` keep working; new
// tests should prefer the interface (`NetTransport *`).
using NetMiddleware = InProcessNetTransport;
