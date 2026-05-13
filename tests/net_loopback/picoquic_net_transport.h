// CHI-101 Phase A — `PicoquicNetTransport`: implements the
// `NetTransport` interface backed by a real two-context picoquic
// pair over localhost UDP datagrams.
//
// Semantics:
//
//   * `send(packet, now_ms)` buffers the packet in an outbox. `now_ms`
//     is ignored — QUIC delivers in wall-clock time.
//   * `drain(now_ms)` flushes the outbox by running one round-trip
//     through the picoquic_loopback_core harness: client queues every
//     buffered packet via picoquic_queue_datagram_frame, server
//     receives them via picoquic_callback_datagram, the round-trip
//     completes, and drain returns whatever the server actually
//     observed. `now_ms` is ignored.
//
// The batched model trades real-time fidelity for simplicity — we
// stand up a fresh client+server pair on each drain (~30 ms warm-up
// + handshake overhead). Phase B can swap this for a long-lived
// streaming transport with shared connection state; the
// observer/agreement tests only care about byte-exact round-trip.

#pragma once

#include "middleware.h"
#include "picoquic_loopback_core.h"

#include <cstring>
#include <vector>

struct PicoquicNetTransport : public NetTransport {
	std::vector<std::vector<uint8_t>> outbox;
	int next_sequence = 1;

	PicoquicNetTransport() = default;

	const char *backend_name() const override { return "picoquic"; }

	bool send(const PackedByteArray &packet, double /*now_ms*/) override {
		++sent;
		std::vector<uint8_t> bytes(static_cast<size_t>(packet.size()));
		if (!bytes.empty()) {
			std::memcpy(bytes.data(), packet.ptr(), bytes.size());
		}
		outbox.push_back(std::move(bytes));
		return true;
	}

	std::vector<DeliveredPacket> drain(double /*now_ms*/) override {
		std::vector<DeliveredPacket> out;
		if (outbox.empty()) {
			return out;
		}

		const auto received = picoquic_loopback::run_passthrough_batch(outbox);

		for (const auto &p : received) {
			DeliveredPacket d;
			d.packet.resize(static_cast<int>(p.size()));
			if (!p.empty()) {
				std::memcpy(d.packet.ptrw(), p.data(), p.size());
			}
			d.sequence_id = next_sequence++;
			out.push_back(std::move(d));
			++delivered;
		}

		// Anything queued but not delivered counts as dropped at the
		// transport boundary (kernel buffer overflow or picoquic
		// queue overflow).
		const int unsent = static_cast<int>(outbox.size()) - static_cast<int>(received.size());
		if (unsent > 0) {
			dropped += unsent;
		}
		outbox.clear();
		return out;
	}
};
