// CHI-101 Phase A — net_loopback step 4 smoke (picoquic vendoring).
//
// Constructs a picoquic_quic_t context with a no-op stream callback,
// reports the library version, and frees the context. Doesn't open
// sockets, doesn't negotiate TLS — proves the FetchContent vendoring
// + header path + link work end-to-end.
//
// Real QUIC datagram loopback (a follow-up sub-pass) will:
//   * spin up two picoquic_quic_t contexts on localhost UDP ports
//   * connect them with a QUIC datagram-only application
//   * route NetMiddleware send() calls through `picoquic_queue_datagram_frame`
//   * route received datagrams into Speech::on_received_audio_packet

#include "picoquic.h"

#include <cstdio>

int main() {
	std::printf("picoquic_smoke: picoquic version = %s\n", PICOQUIC_VERSION);

	// Construct a minimal quic context — server-side with the
	// default settings; no certificates loaded; no stream callback
	// fires because we never accept a connection. Just exercises
	// the alloc + free paths.
	const uint64_t current_time = 0;
	picoquic_quic_t *quic = picoquic_create(
			/*nb_connections=*/8,
			/*cert_file_name=*/nullptr,
			/*key_file_name=*/nullptr,
			/*cert_root_file_name=*/nullptr,
			/*alpn=*/nullptr,
			/*default_callback_fn=*/nullptr,
			/*default_callback_ctx=*/nullptr,
			/*cnx_id_callback_fn=*/nullptr,
			/*cnx_id_callback_ctx=*/nullptr,
			/*reset_seed=*/nullptr,
			current_time,
			/*p_simulated_time=*/nullptr,
			/*ticket_file_name=*/nullptr,
			/*ticket_encryption_key=*/nullptr,
			/*ticket_encryption_key_length=*/0);

	if (!quic) {
		std::fprintf(stderr, "FAIL: picoquic_create returned null\n");
		return 1;
	}
	std::printf("picoquic_smoke: picoquic_quic_t allocated\n");

	picoquic_free(quic);
	std::printf("picoquic_smoke: PASS — picoquic linked + alloc/free round-trip\n");
	return 0;
}
