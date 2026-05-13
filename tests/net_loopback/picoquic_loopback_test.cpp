// CHI-101 Phase A — net_loopback step 4b: real picoquic datagram loopback.
//
// Replaces the in-process NetMiddleware with an actual two-context
// UDP loopback over QUIC:
//
//   * Server runs in a background thread via
//     picoquic_start_network_thread, bound to 127.0.0.1:<ephemeral>.
//   * Client runs picoquic_packet_loop on the main thread.
//   * Both contexts negotiate max_datagram_frame_size via the
//     picoquic_tp_max_datagram_frame_size transport parameter.
//   * Client queues N datagrams via picoquic_queue_datagram_frame on
//     picoquic_callback_ready and counts acks via
//     picoquic_callback_datagram_acked.
//   * Server collects payloads via picoquic_callback_datagram into a
//     mutex-protected vector.
//
// The test asserts that the server received exactly N datagrams with
// payloads matching what the client sent. This closes the
// vendoring-smoke loop (sub-pass 4a) into a real round-trip and is
// the foundation for routing the speech engine through an actual
// QUIC datagram channel in follow-up work.
//
// Uses the picoquic-shipped test cert (CN=test.example.com) at
// $picoquic_SOURCE_DIR/certs/{cert,key}.pem, injected via the
// PICOQUIC_LOOPBACK_CERT_PATH / _KEY_PATH compile definitions. The
// client installs picoquic_set_null_verifier because the test cert
// is not signed by any real CA.

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

namespace {
constexpr const char *kAlpn = "voice-loopback";
constexpr const char *kSni = "test.example.com";
constexpr int kDatagramCount = 32;
constexpr size_t kDatagramSize = 64;
constexpr uint32_t kMaxDatagramFrameSize = 1500;
constexpr uint64_t kCloseGraceUs = 200'000; // 200 ms

struct ServerCtx {
	std::mutex mu;
	std::vector<std::vector<uint8_t>> received;
	std::atomic<int> connections_seen{ 0 };
};

struct ClientCtx {
	picoquic_cnx_t *cnx = nullptr;
	int datagrams_queued = 0;
	int datagrams_acked = 0;
	bool ready = false;
	uint64_t done_marked_us = 0;
	bool close_called = false;
};

void fill_payload(uint8_t *buf, size_t len, int seq) {
	std::memset(buf, 0, len);
	buf[0] = static_cast<uint8_t>(seq & 0xff);
	buf[1] = static_cast<uint8_t>((seq >> 8) & 0xff);
	buf[2] = static_cast<uint8_t>((seq >> 16) & 0xff);
	buf[3] = static_cast<uint8_t>((seq >> 24) & 0xff);
	for (size_t b = 4; b < len; ++b) {
		buf[b] = static_cast<uint8_t>((seq + b) & 0xff);
	}
}

int server_callback(picoquic_cnx_t *cnx,
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

int client_callback(picoquic_cnx_t *cnx,
		uint64_t /*stream_id*/, uint8_t * /*bytes*/, size_t /*length*/,
		picoquic_call_back_event_t event, void *callback_ctx, void * /*v_stream_ctx*/) {
	auto *ctx = static_cast<ClientCtx *>(callback_ctx);
	if (ctx == nullptr) {
		return 0;
	}
	switch (event) {
		case picoquic_callback_ready: {
			ctx->ready = true;
			uint8_t payload[kDatagramSize];
			for (int i = 0; i < kDatagramCount; ++i) {
				fill_payload(payload, sizeof(payload), i);
				int rc = picoquic_queue_datagram_frame(cnx, sizeof(payload), payload);
				if (rc != 0) {
					std::fprintf(stderr, "picoquic_queue_datagram_frame[%d] = %d\n", i, rc);
					return -1;
				}
				ctx->datagrams_queued++;
			}
		} break;
		case picoquic_callback_datagram_acked:
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

// Background thread invokes the loop callback for wake-up and a few
// other events even when we don't subscribe to them, so we have to
// supply a non-null callback that just returns 0.
int server_loop_cb(picoquic_quic_t * /*quic*/, picoquic_packet_loop_cb_enum /*cb_mode*/,
		void * /*callback_ctx*/, void * /*callback_arg*/) {
	return 0;
}

int client_loop_cb(picoquic_quic_t * /*quic*/, picoquic_packet_loop_cb_enum cb_mode,
		void *callback_ctx, void * /*callback_arg*/) {
	auto *ctx = static_cast<ClientCtx *>(callback_ctx);
	if (ctx == nullptr) {
		return PICOQUIC_ERROR_UNEXPECTED_ERROR;
	}
	if (cb_mode == picoquic_packet_loop_after_send || cb_mode == picoquic_packet_loop_after_receive) {
		if (ctx->datagrams_acked >= kDatagramCount) {
			const uint64_t now_us = picoquic_current_time();
			if (!ctx->close_called) {
				picoquic_close(ctx->cnx, 0);
				ctx->close_called = true;
				ctx->done_marked_us = now_us;
			} else if (now_us - ctx->done_marked_us > kCloseGraceUs) {
				return PICOQUIC_NO_ERROR_TERMINATE_PACKET_LOOP;
			}
		}
	}
	return 0;
}

int pick_free_udp_port() {
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
} // namespace

int main() {
	std::printf("picoquic_loopback: starting (picoquic %s)\n", PICOQUIC_VERSION);

	const int server_port = pick_free_udp_port();
	if (server_port <= 0) {
		std::fprintf(stderr, "FAIL: pick_free_udp_port failed\n");
		return 1;
	}
	std::printf("picoquic_loopback: server bound port = %d\n", server_port);

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
		return 1;
	}
	picoquic_set_default_tp_value(server_quic, picoquic_tp_max_datagram_frame_size, kMaxDatagramFrameSize);

	picoquic_packet_loop_param_t loop_param{};
	loop_param.local_port = static_cast<uint16_t>(server_port);
	loop_param.local_af = AF_INET;

	int thread_ret = 0;
	picoquic_network_thread_ctx_t *server_thread = picoquic_start_network_thread(
			server_quic, &loop_param, server_loop_cb, nullptr, &thread_ret);
	if (server_thread == nullptr || thread_ret != 0) {
		std::fprintf(stderr, "FAIL: picoquic_start_network_thread = %d\n", thread_ret);
		picoquic_free(server_quic);
		return 1;
	}

	// Give the server thread a moment to come up and bind its socket.
	std::this_thread::sleep_for(std::chrono::milliseconds(50));

	ClientCtx client_ctx;
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
		return 1;
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
		return 1;
	}
	client_ctx.cnx = cnx;
	picoquic_set_callback(cnx, client_callback, &client_ctx);

	int rc = picoquic_start_client_cnx(cnx);
	if (rc != 0) {
		std::fprintf(stderr, "FAIL: picoquic_start_client_cnx = %d\n", rc);
		picoquic_delete_network_thread(server_thread);
		picoquic_free(client_quic);
		picoquic_free(server_quic);
		return 1;
	}

	rc = picoquic_packet_loop(client_quic, 0, AF_INET, 0, 0, 0,
			client_loop_cb, &client_ctx);
	if (rc != 0 && rc != PICOQUIC_NO_ERROR_TERMINATE_PACKET_LOOP) {
		std::fprintf(stderr, "WARN: picoquic_packet_loop returned %d\n", rc);
	}

	// Give the server one tick to drain the close before we tear it down.
	picoquic_wake_up_network_thread(server_thread);
	std::this_thread::sleep_for(std::chrono::milliseconds(100));

	picoquic_delete_network_thread(server_thread);
	picoquic_free(client_quic);
	picoquic_free(server_quic);

	int received_count = 0;
	std::vector<std::vector<uint8_t>> received;
	{
		std::lock_guard<std::mutex> lock(server_ctx.mu);
		received_count = static_cast<int>(server_ctx.received.size());
		received = server_ctx.received;
	}

	std::printf("picoquic_loopback: queued=%d acked=%d server_received=%d (connections=%d)\n",
			client_ctx.datagrams_queued, client_ctx.datagrams_acked, received_count,
			server_ctx.connections_seen.load());

	int fails = 0;
	if (client_ctx.datagrams_queued != kDatagramCount) {
		std::fprintf(stderr, "FAIL: client queued %d datagrams (expected %d)\n",
				client_ctx.datagrams_queued, kDatagramCount);
		++fails;
	}
	if (received_count != kDatagramCount) {
		std::fprintf(stderr, "FAIL: server received %d datagrams (expected %d)\n",
				received_count, kDatagramCount);
		++fails;
	}
	if (server_ctx.connections_seen.load() == 0) {
		std::fprintf(stderr, "FAIL: server never reached picoquic_callback_ready\n");
		++fails;
	}

	std::vector<bool> seen(kDatagramCount, false);
	for (size_t i = 0; i < received.size(); ++i) {
		const auto &dg = received[i];
		if (dg.size() != kDatagramSize) {
			std::fprintf(stderr, "FAIL: datagram[%zu] size=%zu (expected %zu)\n",
					i, dg.size(), kDatagramSize);
			++fails;
			continue;
		}
		const uint32_t seq = static_cast<uint32_t>(dg[0]) |
				(static_cast<uint32_t>(dg[1]) << 8) |
				(static_cast<uint32_t>(dg[2]) << 16) |
				(static_cast<uint32_t>(dg[3]) << 24);
		if (seq >= static_cast<uint32_t>(kDatagramCount)) {
			std::fprintf(stderr, "FAIL: datagram[%zu] decoded seq=%u out of range\n", i, seq);
			++fails;
			continue;
		}
		seen[seq] = true;
		uint8_t expected[kDatagramSize];
		fill_payload(expected, sizeof(expected), static_cast<int>(seq));
		if (std::memcmp(dg.data(), expected, kDatagramSize) != 0) {
			std::fprintf(stderr, "FAIL: datagram[%zu] payload mismatch for seq=%u\n", i, seq);
			++fails;
		}
	}
	for (int i = 0; i < kDatagramCount; ++i) {
		if (!seen[i]) {
			std::fprintf(stderr, "FAIL: server never received seq=%d\n", i);
			++fails;
		}
	}

	if (fails == 0) {
		std::printf("picoquic_loopback: PASS — %d datagrams round-tripped over real QUIC\n",
				kDatagramCount);
		return 0;
	}
	std::fprintf(stderr, "picoquic_loopback: %d FAIL\n", fails);
	return 1;
}
