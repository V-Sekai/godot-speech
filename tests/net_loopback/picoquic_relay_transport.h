// CHI-101 Phase A — `PicoquicRelayNetTransport`: real QUIC datagrams
// routed through an in-process UDP relay that applies a
// NetworkProfile (latency, jitter, loss, reorder). Lets the audio
// pipeline exercise WAN / lossy conditions end-to-end through the
// real picoquic stack.
//
// `drain()` runs one batch round-trip:
//   1. Bind a fresh UDP socket → that's the server port.
//   2. Start picoquic server on the server port (background thread).
//   3. Start UDPRelayProxy: binds its own port, forwards to server,
//      applies the profile.
//   4. Pick a free local port for the client (handled by picoquic).
//   5. Start picoquic client connecting to the relay's front port.
//   6. Client queues every outbox entry via picoquic_queue_datagram_frame.
//   7. Server picks up datagrams via picoquic_callback_datagram.
//   8. Tear everything down.
//
// Counters: `sent` (audit of submitted), `dropped` (relay-level loss
// plus picoquic-level drops), `reordered` (relay reorders),
// `delivered` (what the server actually received).

#pragma once

#include "middleware.h"
#include "picoquic_loopback_core.h"
#include "udp_relay.h"

#include <cstring>
#include <vector>

struct PicoquicRelayNetTransport : public NetTransport {
	NetworkProfile profile;
	std::vector<std::vector<uint8_t>> outbox;
	int next_sequence = 1;

	explicit PicoquicRelayNetTransport(NetworkProfile p) :
			profile(p) {}

	const char *backend_name() const override { return "picoquic + relay"; }

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

		// Pick the server port up-front so the relay knows where to
		// forward to. The picoquic loopback core's internal port
		// pick would also pick "127.0.0.1:port" — we just take that
		// over here so we can wire the relay in front of it.
		const int server_port_int = picoquic_loopback::pick_free_udp_port();
		if (server_port_int <= 0) {
			outbox.clear();
			return out;
		}

		sockaddr_in server_addr{};
		server_addr.sin_family = AF_INET;
		server_addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
		server_addr.sin_port = htons(static_cast<uint16_t>(server_port_int));

		UDPRelayProxy relay(server_addr, profile);
		const uint16_t relay_port = relay.start();
		if (relay_port == 0) {
			outbox.clear();
			return out;
		}

		// Stand up the picoquic server on the pre-bound server_port
		// and have the client connect to relay_port. Reuses the same
		// harness as the no-relay variant; the relay is invisible
		// to picoquic.
		picoquic_loopback::PassthroughBatchOptions opts;
		opts.relay_port_override = relay_port;
		opts.server_port_override = server_port_int;
		// Generous deadline so the simulated WAN profile (RTT 160 ms+)
		// doesn't get cut off by the deadline timer mid-handshake.
		opts.test_timeout_ms = 8'000;
		const auto received = picoquic_loopback::run_passthrough_batch_ex(outbox, opts);

		relay.stop();

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
		const auto &c = relay.counters();
		reordered += c.c2s_reordered.load() + c.s2c_reordered.load();
		const int unsent = static_cast<int>(outbox.size()) - static_cast<int>(received.size());
		if (unsent > 0) {
			dropped += unsent;
		}
		outbox.clear();
		return out;
	}
};
