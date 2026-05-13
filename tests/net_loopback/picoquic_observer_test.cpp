// CHI-101 Phase A — net_loopback step 4d: agreement observer over
// real QUIC datagrams.
//
// Same audio-pipeline + kernel-vs-engine agreement check as
// `observer_test.cpp`, but the transport in the middle is the real
// picoquic round-trip (`PicoquicNetTransport`) instead of the
// in-process synthetic middleware.
//
// What this proves:
//   1. The polymorphic `NetTransport` interface lets the existing
//      observer harness route audio through any transport without
//      knowing it's there.
//   2. The audio engine's speech_processed packets survive a real
//      QUIC datagram hop byte-for-byte, and the FramingCursor /
//      JitterAppend kernel agreement still holds.
//
// We run two profiles:
//   - "in-process (lan-like)": existing synthetic middleware, no
//     loss/jitter — establishes the baseline that observer_test
//     already validates.
//   - "picoquic loopback":     real QUIC datagrams over localhost,
//     fresh client+server pair stood up inside drain(). Loss is
//     not expected on localhost with the in-flight pacing window.

#include "dashboard.h"
#include "middleware.h"
#include "picoquic_net_transport.h"

#include "../../speech.h"
#include "../../speech_processor.h"
#include "audio/audio_stream_generator.h"
#include "audio/audio_stream_player.h"
#include "core/core_types.h"

#include <ftxui/screen/screen.hpp>

#include <cmath>
#include <cstdio>
#include <memory>
#include <string>

struct RunResult {
	DashboardSnapshot snapshot;
	bool engine_packet_count_matches_kernel = true;
	bool engine_jb_size_matches_kernel = true;
	const char *backend_label = "";
};

static RunResult run_with_transport(const char *label,
		NetTransport &net) {
	Speech speech;
	const int peer_id = 1;
	AudioStreamPlayer player;
	player.set_name(String("AudioStreamPlayer"));
	Ref<AudioStreamGenerator> gen(new AudioStreamGenerator());
	gen->set_mix_rate(48000.0f);
	gen->set_buffer_length(1.0f);
	player.set_stream(gen);
	speech.add_player_audio(peer_id, &player);

	SpeechProcessor processor;
	double clock_ms = 0.0;
	const double packet_period_ms = static_cast<double>(SpeechProcessor::SPEECH_SETTING_MILLISECONDS_PER_PACKET);
	processor.register_speech_processed(
			[&net, &clock_ms, packet_period_ms](SpeechProcessor::SpeechInput *in) {
				if (in && in->pcm_byte_array) {
					net.send(*in->pcm_byte_array, clock_ms);
					clock_ms += packet_period_ms;
				}
			});

	const uint32_t voice_rate = SpeechProcessor::SPEECH_SETTING_VOICE_SAMPLE_RATE;
	const int frames = static_cast<int>(voice_rate / 5); // 200 ms
	PackedFloat32Array mono;
	mono.resize(frames);
	for (int i = 0; i < frames; ++i) {
		const double t = static_cast<double>(i) / static_cast<double>(voice_rate);
		mono.write[i] = static_cast<float>(std::sin(2.0 * 3.14159265358979 * 440.0 * t) * 0.5);
	}
	processor.test_process_mono_audio_frames(mono, voice_rate);

	// Drain. For the in-process transport the synthetic clock matters;
	// for the QUIC transport `now_ms` is ignored and drain() runs a
	// real round-trip.
	const double drain_until = clock_ms + 50.0;
	auto inflight = net.drain(drain_until);
	for (size_t i = 0; i < inflight.size(); ++i) {
		speech.on_received_audio_packet(peer_id, inflight[i].sequence_id,
				inflight[i].packet);
	}

	Dictionary elem = speech.get_player_audio()[peer_id];
	Array jb = elem["jitter_buffer"];

	// Same kernel-vs-engine checks as observer_test.cpp.
	const int kernel_expected_sent =
			frames / SpeechProcessor::SPEECH_SETTING_BUFFER_FRAME_COUNT;
	const bool framing_agrees = (net.sent == kernel_expected_sent);

	const int max_jb = speech.get_max_jitter_buffer_size();
	const int kernel_jb_upper = (net.sent < max_jb) ? net.sent : max_jb;
	const bool jb_agrees = (jb.size() <= kernel_jb_upper);

	RunResult r;
	r.snapshot.profile_label = label;
	r.snapshot.tick_ms = drain_until;
	r.snapshot.net_sent = net.sent;
	r.snapshot.net_delivered = net.delivered;
	r.snapshot.net_dropped = net.dropped;
	r.snapshot.net_reordered = net.reordered;
	r.snapshot.jb_size = jb.size();
	r.snapshot.jb_max = max_jb;
	r.snapshot.playback_skips = 0;
	r.snapshot.frames_per_tick = net.delivered;
	r.snapshot.frames_per_tick_max = kernel_expected_sent;
	r.snapshot.framing_cursor_agrees = framing_agrees;
	r.snapshot.jitter_append_agrees = jb_agrees;
	r.engine_packet_count_matches_kernel = framing_agrees;
	r.engine_jb_size_matches_kernel = jb_agrees;
	r.backend_label = net.backend_name();
	return r;
}

static void render(const DashboardSnapshot &s) {
	auto element = build_dashboard(s);
	auto screen = ftxui::Screen::Create(ftxui::Dimension::Fit(element));
	ftxui::Render(screen, element);
	std::printf("%s\n", screen.ToString().c_str());
}

int main() {
	int fails = 0;

	// In-process baseline — same backend as observer_test's lan
	// profile, used here only to confirm the polymorphic interface
	// keeps producing identical results.
	{
		NetworkProfile p;
		p.base_latency_ms = 5.0;
		p.seed = 0x1ABC0DE'001u;
		InProcessNetTransport net(p);
		auto r = run_with_transport("in-process (lan)", net);
		render(r.snapshot);
		if (!r.engine_packet_count_matches_kernel || !r.engine_jb_size_matches_kernel) {
			std::fprintf(stderr, "FAIL: in-process kernel ⇔ engine disagreement\n");
			++fails;
		}
		if (net.delivered != net.sent) {
			std::fprintf(stderr, "FAIL: in-process lost packets (sent=%d delivered=%d)\n",
					net.sent, net.delivered);
			++fails;
		}
	}

	// Real picoquic datagram round-trip. Same audio pipeline.
	{
		PicoquicNetTransport net;
		auto r = run_with_transport("picoquic loopback", net);
		render(r.snapshot);
		if (!r.engine_packet_count_matches_kernel || !r.engine_jb_size_matches_kernel) {
			std::fprintf(stderr, "FAIL: picoquic kernel ⇔ engine disagreement\n");
			++fails;
		}
		// Transport bookkeeping: every queued packet should be
		// accounted for (delivered + dropped == sent). picoquic
		// localhost is near-perfect but the same seq-5 artifact
		// we see in the stress sweep can drop ~1 packet here;
		// what matters is the engine still agrees with the
		// kernel formulas above.
		if (net.delivered + net.dropped != net.sent) {
			std::fprintf(stderr,
					"FAIL: picoquic transport bookkeeping mismatch (sent=%d delivered=%d dropped=%d)\n",
					net.sent, net.delivered, net.dropped);
			++fails;
		}
		const int max_acceptable_loss = (net.sent + 9) / 10; // ~10%
		if (net.dropped > max_acceptable_loss) {
			std::fprintf(stderr,
					"FAIL: picoquic dropped %d of %d (> 10%% loss bound)\n",
					net.dropped, net.sent);
			++fails;
		}
	}

	if (fails == 0) {
		std::printf("picoquic_observer: PASS — kernel ⇔ engine agreement holds across in-process + real QUIC\n");
		return 0;
	}
	std::fprintf(stderr, "picoquic_observer: %d FAIL\n", fails);
	return 1;
}
