// CHI-101 Phase A — synthetic network middleware for the loopback
// driver. Sits between SpeechProcessor's `speech_processed`
// callback (the sender) and Speech::on_received_audio_packet
// (the receiver), applying configurable jitter / loss / reorder
// before delivering each packet.
//
// Headless and deterministic — every behavior is driven from a
// seeded `std::mt19937` so the same NetworkProfile produces the
// same sequence of delays / drops / swaps across runs.
//
// Tick semantics: the test driver increments a synthetic clock
// (`tick_ms`) before each delivery cycle. The middleware queues
// packets with a `deliver_at_ms = now + delay`. `drain(now)`
// returns every packet whose deliver_at_ms <= now, in arrival
// order (the reorder window jumbles arrival before queue, not
// delivery — matches how a real network reorders out-of-flight
// datagrams).

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

struct NetMiddleware {
	struct InFlight {
		PackedByteArray packet;
		int sequence_id = 0; // sender's view of order; preserved across drop/reorder
		double deliver_at_ms = 0.0;
	};

	NetworkProfile profile;
	std::mt19937_64 rng;
	std::deque<InFlight> queue;
	int next_sequence = 1;

	// Counters — accumulate across the whole run.
	int sent = 0;
	int dropped = 0;
	int reordered = 0;
	int delivered = 0;

	explicit NetMiddleware(NetworkProfile p) :
			profile(p), rng(p.seed) {}

	// Submit a packet from the sender side at `now_ms`. Returns
	// false if the packet was dropped; otherwise the packet is
	// queued for later `drain`.
	bool send(const PackedByteArray &packet, double now_ms) {
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

	// Pop every packet whose deliver_at_ms <= now_ms, in queue
	// order. The caller hands each to the receiver.
	std::vector<InFlight> drain(double now_ms) {
		std::vector<InFlight> out;
		while (!queue.empty() && queue.front().deliver_at_ms <= now_ms) {
			out.push_back(queue.front());
			queue.pop_front();
			++delivered;
		}
		return out;
	}
};
