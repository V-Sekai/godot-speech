// CHI-101 Phase A — shared loopback harness for the real-QUIC tests.
//
// Both `picoquic_loopback_test.cpp` (small sanity check) and
// `picoquic_stress_test.cpp` (count + payload sweep) call
// `run_picoquic_loopback(cfg)` with different (count, size) configs
// and let it stand up the same client / server / packet-loop dance.
//
// Returns a `LoopbackResult` with the counts the test driver
// asserts on: queued + acked from the client's side, and the bytes
// the server saw via picoquic_callback_datagram. The result also
// records the wall-clock duration so the stress driver can print
// throughput numbers.

#pragma once

#include <picoquic.h>
#include <picoquic_packet_loop.h>

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <thread>
#include <vector>

#ifndef PICOQUIC_LOOPBACK_CERT_PATH
#error "PICOQUIC_LOOPBACK_CERT_PATH must be defined by the build"
#endif
#ifndef PICOQUIC_LOOPBACK_KEY_PATH
#error "PICOQUIC_LOOPBACK_KEY_PATH must be defined by the build"
#endif

namespace picoquic_loopback {
constexpr const char *kAlpn = "voice-loopback";
constexpr const char *kSni = "test.example.com";
constexpr uint32_t kMaxDatagramFrameSize = 1500;
constexpr uint64_t kCloseGraceUs = 200'000;

struct LoopbackConfig {
	int datagram_count = 32;
	size_t datagram_size = 64;
	// How long to wait for the round-trip to complete before declaring
	// the test stuck. 0 = derive from datagram_count.
	uint64_t test_timeout_ms = 0;
	// Maximum unacked datagrams in flight at any moment. New ones are
	// queued on every datagram_acked. Keeps the send + recv socket
	// buffers from overflowing on bursty configs. 2 is a safe choice
	// for sustained throughput; 8 is good for low-count bursts.
	int in_flight_window = 2;
	// UDP socket SO_RCVBUF / SO_SNDBUF in bytes. 0 = picoquic default
	// (~64 KB). 2 MB safely covers all sweep configurations.
	int socket_buffer_size = 2 * 1024 * 1024;
};

struct LoopbackResult {
	int datagrams_queued = 0;
	int datagrams_acked = 0;
	int datagrams_received = 0;
	int server_connections_seen = 0;
	int payload_mismatches = 0;
	int missing_sequences = 0;
	double elapsed_ms = 0.0;
	bool timed_out = false;
};

struct ServerCtx {
	std::mutex mu;
	std::vector<std::vector<uint8_t>> received;
	std::atomic<int> connections_seen{ 0 };
};

struct ClientCtx {
	picoquic_cnx_t *cnx = nullptr;
	int datagrams_queued = 0;
	int datagrams_acked = 0;
	int datagrams_lost = 0; // picoquic gave up on these
	int last_progress_count = 0; // acked + lost from last progress tick
	int target_count = 0;
	// Synthetic-mode payload: every datagram is target_size bytes,
	// filled by `fill_payload(seq)`.
	size_t target_size = 0;
	// Passthrough-mode payload: each datagram comes from
	// `(*passthrough_payloads)[seq]`. When non-null, target_size is
	// ignored.
	const std::vector<std::vector<uint8_t>> *passthrough_payloads = nullptr;
	int in_flight_window = 8;
	uint64_t all_queued_at_us = 0;
	uint64_t last_progress_us = 0;
	uint64_t done_marked_us = 0;
	uint64_t stall_grace_us = 2'000'000; // 2 s with no ack/lost progress → close
	bool close_called = false;
};

inline void fill_payload(uint8_t *buf, size_t len, int seq) {
	std::memset(buf, 0, len);
	buf[0] = static_cast<uint8_t>(seq & 0xff);
	buf[1] = static_cast<uint8_t>((seq >> 8) & 0xff);
	buf[2] = static_cast<uint8_t>((seq >> 16) & 0xff);
	buf[3] = static_cast<uint8_t>((seq >> 24) & 0xff);
	for (size_t b = 4; b < len; ++b) {
		buf[b] = static_cast<uint8_t>((seq + b) & 0xff);
	}
}

// Top up the in-flight window: queue datagrams until either the
// target count is reached or the window (queued - resolved) is full.
inline int refill_in_flight(ClientCtx *ctx) {
	std::vector<uint8_t> synthetic_payload;
	if (ctx->passthrough_payloads == nullptr) {
		synthetic_payload.resize(ctx->target_size);
	}
	while (ctx->datagrams_queued < ctx->target_count) {
		const int resolved = ctx->datagrams_acked + ctx->datagrams_lost;
		const int in_flight = ctx->datagrams_queued - resolved;
		if (in_flight >= ctx->in_flight_window) {
			break;
		}
		const int seq = ctx->datagrams_queued;
		const uint8_t *bytes = nullptr;
		size_t len = 0;
		if (ctx->passthrough_payloads != nullptr) {
			const auto &p = (*ctx->passthrough_payloads)[seq];
			bytes = p.data();
			len = p.size();
		} else {
			fill_payload(synthetic_payload.data(), synthetic_payload.size(), seq);
			bytes = synthetic_payload.data();
			len = synthetic_payload.size();
		}
		int rc = picoquic_queue_datagram_frame(ctx->cnx, len, bytes);
		if (rc != 0) {
			std::fprintf(stderr, "picoquic_queue_datagram_frame[%d] = %d\n", seq, rc);
			return rc;
		}
		ctx->datagrams_queued++;
	}
	return 0;
}

inline int server_callback(picoquic_cnx_t *cnx,
		uint64_t /*stream_id*/, uint8_t *bytes, size_t length,
		picoquic_call_back_event_t event, void *callback_ctx, void * /*v_stream_ctx*/) {
	auto *ctx = static_cast<ServerCtx *>(callback_ctx);
	if (ctx == nullptr) {
		return 0;
	}
	switch (event) {
		case picoquic_callback_almost_ready:
		case picoquic_callback_ready:
			ctx->connections_seen.fetch_add(1, std::memory_order_release);
			break;
		case picoquic_callback_datagram: {
			std::lock_guard<std::mutex> lock(ctx->mu);
			ctx->received.emplace_back(bytes, bytes + length);
		} break;
		case picoquic_callback_close:
		case picoquic_callback_application_close:
		case picoquic_callback_stateless_reset:
			picoquic_set_callback(cnx, nullptr, nullptr);
			break;
		default:
			break;
	}
	return 0;
}

inline int client_callback(picoquic_cnx_t *cnx,
		uint64_t /*stream_id*/, uint8_t * /*bytes*/, size_t /*length*/,
		picoquic_call_back_event_t event, void *callback_ctx, void * /*v_stream_ctx*/) {
	auto *ctx = static_cast<ClientCtx *>(callback_ctx);
	if (ctx == nullptr) {
		return 0;
	}
	switch (event) {
		case picoquic_callback_ready:
			if (refill_in_flight(ctx) != 0) {
				return -1;
			}
			break;
		case picoquic_callback_datagram_acked:
			ctx->datagrams_acked++;
			if (refill_in_flight(ctx) != 0) {
				return -1;
			}
			break;
		case picoquic_callback_datagram_lost:
			ctx->datagrams_lost++;
			if (refill_in_flight(ctx) != 0) {
				return -1;
			}
			break;
		case picoquic_callback_datagram_spurious:
			// Previously thought-lost packet did make it. Reclassify.
			if (ctx->datagrams_lost > 0) {
				ctx->datagrams_lost--;
			}
			ctx->datagrams_acked++;
			break;
		case picoquic_callback_close:
		case picoquic_callback_application_close:
		case picoquic_callback_stateless_reset:
			picoquic_set_callback(cnx, nullptr, nullptr);
			break;
		default:
			break;
	}
	return 0;
}

inline int server_loop_cb(picoquic_quic_t * /*quic*/, picoquic_packet_loop_cb_enum /*cb_mode*/,
		void * /*callback_ctx*/, void * /*callback_arg*/) {
	return 0;
}

struct ClientLoopState {
	ClientCtx *client = nullptr;
	uint64_t deadline_us = 0;
};

inline int client_loop_cb(picoquic_quic_t * /*quic*/, picoquic_packet_loop_cb_enum cb_mode,
		void *callback_ctx, void *callback_arg) {
	auto *state = static_cast<ClientLoopState *>(callback_ctx);
	if (state == nullptr || state->client == nullptr) {
		return PICOQUIC_ERROR_UNEXPECTED_ERROR;
	}
	const uint64_t now_us = picoquic_current_time();
	if (state->deadline_us != 0 && now_us > state->deadline_us) {
		std::fprintf(stderr, "picoquic_loopback: client deadline exceeded — forcing exit\n");
		return PICOQUIC_NO_ERROR_TERMINATE_PACKET_LOOP;
	}
	// Once close has been initiated, exit on the next callback of any
	// kind so the test isn't stuck waiting for after_send during
	// picoquic's draining state.
	if (state->client->close_called) {
		return PICOQUIC_NO_ERROR_TERMINATE_PACKET_LOOP;
	}
	switch (cb_mode) {
		case picoquic_packet_loop_ready: {
			// Enable time_check so we get periodic wake-ups even when
			// there's no send/receive activity (needed to fire the
			// drain-grace timeout after all datagrams have been
			// queued + ack'd).
			auto *options = static_cast<picoquic_packet_loop_options_t *>(callback_arg);
			if (options != nullptr) {
				options->do_time_check = 1;
			}
		} break;
		case picoquic_packet_loop_time_check: {
			// Cap the proposed wait so we re-check the drain timeout
			// at least every 100 ms.
			auto *arg = static_cast<packet_loop_time_check_arg_t *>(callback_arg);
			if (arg != nullptr) {
				constexpr int64_t kMaxDeltaUs = 100'000;
				if (arg->delta_t > kMaxDeltaUs || arg->delta_t < 0) {
					arg->delta_t = kMaxDeltaUs;
				}
			}
		} break;
		case picoquic_packet_loop_after_send:
			if (state->client->datagrams_queued < state->client->target_count) {
				if (refill_in_flight(state->client) != 0) {
					return PICOQUIC_ERROR_UNEXPECTED_ERROR;
				}
				return 0;
			}
			break;
		default:
			break;
	}
	// Once queueing is done, close when every queued datagram has
	// been resolved (acked or definitively lost), or when no progress
	// has been made for stall_grace_us. Closing too early cancels
	// in-flight datagrams — picoquic_close transitions the cnx into
	// disconnecting state and drops any frames still in the send
	// queue. The stall fallback bounds the wait if picoquic doesn't
	// surface a `datagram_lost` callback for stuck frames.
	if (state->client->datagrams_queued >= state->client->target_count) {
		const int resolved = state->client->datagrams_acked + state->client->datagrams_lost;
		if (state->client->all_queued_at_us == 0) {
			state->client->all_queued_at_us = now_us;
			state->client->last_progress_us = now_us;
			state->client->last_progress_count = resolved;
		}
		if (resolved > state->client->last_progress_count) {
			state->client->last_progress_count = resolved;
			state->client->last_progress_us = now_us;
		}
		const bool all_resolved = resolved >= state->client->target_count;
		const bool stalled = (now_us - state->client->last_progress_us) > state->client->stall_grace_us;
		if (all_resolved || stalled) {
			picoquic_close(state->client->cnx, 0);
			state->client->close_called = true;
			state->client->done_marked_us = now_us;
		}
	}
	return 0;
}

inline int pick_free_udp_port() {
	int fd = ::socket(AF_INET, SOCK_DGRAM, 0);
	if (fd < 0) {
		return -1;
	}
	sockaddr_in addr{};
	addr.sin_family = AF_INET;
	addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
	addr.sin_port = 0;
	if (::bind(fd, reinterpret_cast<sockaddr *>(&addr), sizeof(addr)) != 0) {
		::close(fd);
		return -1;
	}
	socklen_t len = sizeof(addr);
	if (::getsockname(fd, reinterpret_cast<sockaddr *>(&addr), &len) != 0) {
		::close(fd);
		return -1;
	}
	int port = ntohs(addr.sin_port);
	::close(fd);
	return port;
}

inline LoopbackResult run_picoquic_loopback(const LoopbackConfig &cfg) {
	LoopbackResult result;
	const int server_port = pick_free_udp_port();
	if (server_port <= 0) {
		std::fprintf(stderr, "FAIL: pick_free_udp_port failed\n");
		return result;
	}

	ServerCtx server_ctx;
	const uint64_t now_us = picoquic_current_time();
	picoquic_quic_t *server_quic = picoquic_create(
			8,
			PICOQUIC_LOOPBACK_CERT_PATH,
			PICOQUIC_LOOPBACK_KEY_PATH,
			nullptr,
			kAlpn,
			server_callback, &server_ctx,
			nullptr, nullptr, nullptr,
			now_us, nullptr,
			nullptr, nullptr, 0);
	if (server_quic == nullptr) {
		std::fprintf(stderr, "FAIL: server picoquic_create failed\n");
		return result;
	}
	picoquic_set_default_tp_value(server_quic, picoquic_tp_max_datagram_frame_size, kMaxDatagramFrameSize);
	// Short max_idle_timeout so the server's select() in
	// picoquic_packet_loop_select bounds its wait — closing the
	// wake-up pipe from another thread does not reliably interrupt
	// select() on macOS, so we rely on idle-timeout for teardown.
	picoquic_set_default_tp_value(server_quic, picoquic_tp_idle_timeout, 1000);

	picoquic_packet_loop_param_t loop_param{};
	loop_param.local_port = static_cast<uint16_t>(server_port);
	loop_param.local_af = AF_INET;
	loop_param.socket_buffer_size = cfg.socket_buffer_size;

	int thread_ret = 0;
	picoquic_network_thread_ctx_t *server_thread = picoquic_start_network_thread(
			server_quic, &loop_param, server_loop_cb, nullptr, &thread_ret);
	if (server_thread == nullptr || thread_ret != 0) {
		std::fprintf(stderr, "FAIL: picoquic_start_network_thread = %d\n", thread_ret);
		picoquic_free(server_quic);
		return result;
	}
	std::this_thread::sleep_for(std::chrono::milliseconds(50));

	ClientCtx client_ctx;
	client_ctx.target_count = cfg.datagram_count;
	client_ctx.target_size = cfg.datagram_size;
	client_ctx.in_flight_window = cfg.in_flight_window;
	picoquic_quic_t *client_quic = picoquic_create(
			1,
			nullptr, nullptr, nullptr,
			kAlpn,
			nullptr, nullptr,
			nullptr, nullptr, nullptr,
			now_us, nullptr,
			nullptr, nullptr, 0);
	if (client_quic == nullptr) {
		std::fprintf(stderr, "FAIL: client picoquic_create failed\n");
		picoquic_delete_network_thread(server_thread);
		picoquic_free(server_quic);
		return result;
	}
	picoquic_set_default_tp_value(client_quic, picoquic_tp_max_datagram_frame_size, kMaxDatagramFrameSize);
	picoquic_set_null_verifier(client_quic);

	sockaddr_in server_addr{};
	server_addr.sin_family = AF_INET;
	server_addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
	server_addr.sin_port = htons(static_cast<uint16_t>(server_port));

	picoquic_cnx_t *cnx = picoquic_create_cnx(
			client_quic,
			picoquic_null_connection_id, picoquic_null_connection_id,
			reinterpret_cast<sockaddr *>(&server_addr),
			now_us, 0, kSni, kAlpn, /*client_mode=*/1);
	if (cnx == nullptr) {
		std::fprintf(stderr, "FAIL: picoquic_create_cnx failed\n");
		picoquic_delete_network_thread(server_thread);
		picoquic_free(client_quic);
		picoquic_free(server_quic);
		return result;
	}
	client_ctx.cnx = cnx;
	picoquic_set_callback(cnx, client_callback, &client_ctx);
	// Enable keep-alive so the last data datagram's ack isn't held
	// indefinitely by picoquic's ack-delay timer waiting for the
	// next outbound packet to piggyback on.
	picoquic_enable_keep_alive(cnx, 50'000); // 50 ms PING interval

	int rc = picoquic_start_client_cnx(cnx);
	if (rc != 0) {
		std::fprintf(stderr, "FAIL: picoquic_start_client_cnx = %d\n", rc);
		picoquic_delete_network_thread(server_thread);
		picoquic_free(client_quic);
		picoquic_free(server_quic);
		return result;
	}

	uint64_t timeout_ms = cfg.test_timeout_ms;
	if (timeout_ms == 0) {
		// Derive a generous deadline so even very large stress configs
		// don't deadlock. 2 seconds baseline + 5 ms per datagram covers
		// 1k datagrams in ~7 s even on a slow CI runner.
		timeout_ms = 2'000 + static_cast<uint64_t>(cfg.datagram_count) * 5;
	}
	ClientLoopState loop_state;
	loop_state.client = &client_ctx;
	loop_state.deadline_us = picoquic_current_time() + timeout_ms * 1000;

	const auto t_start = std::chrono::steady_clock::now();
	rc = picoquic_packet_loop(client_quic, 0, AF_INET, 0, cfg.socket_buffer_size, 0,
			client_loop_cb, &loop_state);
	const auto t_loop_end = std::chrono::steady_clock::now();

	if (rc != 0 && rc != PICOQUIC_NO_ERROR_TERMINATE_PACKET_LOOP) {
		std::fprintf(stderr, "WARN: picoquic_packet_loop returned %d\n", rc);
	}
	if (client_ctx.datagrams_acked < cfg.datagram_count) {
		result.timed_out = true;
	}

	picoquic_wake_up_network_thread(server_thread);
	picoquic_delete_network_thread(server_thread);
	picoquic_free(client_quic);
	picoquic_free(server_quic);
	result.elapsed_ms = std::chrono::duration<double, std::milli>(t_loop_end - t_start).count();

	std::vector<std::vector<uint8_t>> received;
	{
		std::lock_guard<std::mutex> lock(server_ctx.mu);
		received = std::move(server_ctx.received);
	}
	result.datagrams_queued = client_ctx.datagrams_queued;
	result.datagrams_acked = client_ctx.datagrams_acked;
	result.datagrams_received = static_cast<int>(received.size());
	result.server_connections_seen = server_ctx.connections_seen.load();

	std::vector<bool> seen(cfg.datagram_count, false);
	std::vector<uint8_t> expected(cfg.datagram_size);
	for (const auto &dg : received) {
		if (dg.size() != cfg.datagram_size) {
			++result.payload_mismatches;
			continue;
		}
		const uint32_t seq = static_cast<uint32_t>(dg[0]) |
				(static_cast<uint32_t>(dg[1]) << 8) |
				(static_cast<uint32_t>(dg[2]) << 16) |
				(static_cast<uint32_t>(dg[3]) << 24);
		if (seq >= static_cast<uint32_t>(cfg.datagram_count)) {
			++result.payload_mismatches;
			continue;
		}
		seen[seq] = true;
		fill_payload(expected.data(), expected.size(), static_cast<int>(seq));
		if (std::memcmp(dg.data(), expected.data(), cfg.datagram_size) != 0) {
			++result.payload_mismatches;
		}
	}
	for (int i = 0; i < cfg.datagram_count; ++i) {
		if (!seen[i]) {
			++result.missing_sequences;
		}
	}
	return result;
}

// Variable-payload batch round-trip: feed each entry of `payloads`
// to the client as a datagram, return whatever the server received
// (in arrival order). Used by `PicoquicNetTransport` to ferry the
// audio engine's actual packets through real QUIC.
inline std::vector<std::vector<uint8_t>> run_passthrough_batch(
		const std::vector<std::vector<uint8_t>> &payloads) {
	std::vector<std::vector<uint8_t>> received_out;
	if (payloads.empty()) {
		return received_out;
	}
	const int server_port = pick_free_udp_port();
	if (server_port <= 0) {
		return received_out;
	}

	ServerCtx server_ctx;
	const uint64_t now_us = picoquic_current_time();
	picoquic_quic_t *server_quic = picoquic_create(
			8,
			PICOQUIC_LOOPBACK_CERT_PATH,
			PICOQUIC_LOOPBACK_KEY_PATH,
			nullptr,
			kAlpn,
			server_callback, &server_ctx,
			nullptr, nullptr, nullptr,
			now_us, nullptr,
			nullptr, nullptr, 0);
	if (server_quic == nullptr) {
		return received_out;
	}
	picoquic_set_default_tp_value(server_quic, picoquic_tp_max_datagram_frame_size, kMaxDatagramFrameSize);
	picoquic_set_default_tp_value(server_quic, picoquic_tp_idle_timeout, 1000);

	picoquic_packet_loop_param_t loop_param{};
	loop_param.local_port = static_cast<uint16_t>(server_port);
	loop_param.local_af = AF_INET;
	loop_param.socket_buffer_size = 2 * 1024 * 1024;

	int thread_ret = 0;
	picoquic_network_thread_ctx_t *server_thread = picoquic_start_network_thread(
			server_quic, &loop_param, server_loop_cb, nullptr, &thread_ret);
	if (server_thread == nullptr || thread_ret != 0) {
		picoquic_free(server_quic);
		return received_out;
	}
	std::this_thread::sleep_for(std::chrono::milliseconds(50));

	ClientCtx client_ctx;
	client_ctx.target_count = static_cast<int>(payloads.size());
	client_ctx.passthrough_payloads = &payloads;
	client_ctx.in_flight_window = 2;
	picoquic_quic_t *client_quic = picoquic_create(
			1,
			nullptr, nullptr, nullptr,
			kAlpn,
			nullptr, nullptr,
			nullptr, nullptr, nullptr,
			now_us, nullptr,
			nullptr, nullptr, 0);
	if (client_quic == nullptr) {
		picoquic_delete_network_thread(server_thread);
		picoquic_free(server_quic);
		return received_out;
	}
	picoquic_set_default_tp_value(client_quic, picoquic_tp_max_datagram_frame_size, kMaxDatagramFrameSize);
	picoquic_set_null_verifier(client_quic);

	sockaddr_in server_addr{};
	server_addr.sin_family = AF_INET;
	server_addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
	server_addr.sin_port = htons(static_cast<uint16_t>(server_port));

	picoquic_cnx_t *cnx = picoquic_create_cnx(
			client_quic,
			picoquic_null_connection_id, picoquic_null_connection_id,
			reinterpret_cast<sockaddr *>(&server_addr),
			now_us, 0, kSni, kAlpn, /*client_mode=*/1);
	if (cnx == nullptr) {
		picoquic_delete_network_thread(server_thread);
		picoquic_free(client_quic);
		picoquic_free(server_quic);
		return received_out;
	}
	client_ctx.cnx = cnx;
	picoquic_set_callback(cnx, client_callback, &client_ctx);
	picoquic_enable_keep_alive(cnx, 50'000);
	if (picoquic_start_client_cnx(cnx) != 0) {
		picoquic_delete_network_thread(server_thread);
		picoquic_free(client_quic);
		picoquic_free(server_quic);
		return received_out;
	}

	const uint64_t timeout_ms = 2'000 + static_cast<uint64_t>(payloads.size()) * 5;
	ClientLoopState loop_state;
	loop_state.client = &client_ctx;
	loop_state.deadline_us = picoquic_current_time() + timeout_ms * 1000;

	(void)picoquic_packet_loop(client_quic, 0, AF_INET, 0, 2 * 1024 * 1024, 0,
			client_loop_cb, &loop_state);

	picoquic_wake_up_network_thread(server_thread);
	picoquic_delete_network_thread(server_thread);
	picoquic_free(client_quic);
	picoquic_free(server_quic);

	std::lock_guard<std::mutex> lock(server_ctx.mu);
	received_out = std::move(server_ctx.received);
	return received_out;
}
} // namespace picoquic_loopback
