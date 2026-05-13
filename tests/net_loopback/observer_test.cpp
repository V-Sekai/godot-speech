// CHI-101 Phase A — net_loopback step 3: live loopback + dashboard
// observer + slang_validate kernel agreement.
//
// Drives the same three NetMiddleware profiles as the assertion-
// based driver (step 1), captures per-profile snapshots of the
// engine state, runs the FramingCursor + JitterAppend kernels'
// formulas alongside the engine, and renders the dashboard for
// each profile to stdout via FTXUI.
//
// The agreement check is the high-leverage part: if the kernel
// formulas predict a different value than the engine reports,
// the dashboard's "KERNEL ⇔ ENGINE" row flips to red and the
// test fails. Same regression-guard contract as the
// godot_audio_model_agreement binary, but now wrapped in the
// network-loopback context where future tests will live.

#include "dashboard.h"
#include "middleware.h"

#include "../../speech.h"
#include "../../speech_processor.h"
#include "audio/audio_stream_generator.h"
#include "audio/audio_stream_player.h"
#include "core/core_types.h"

#include <ftxui/screen/screen.hpp>

#include <cmath>
#include <cstdio>
#include <string>

struct RunResult {
	DashboardSnapshot snapshot;
	bool engine_packet_count_matches_kernel = true;
	bool engine_jb_size_matches_kernel = true;
};

static RunResult run_profile(const char *label,
		const NetworkProfile &profile) {
	NetMiddleware net(profile);

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

	// 200 ms of 440 Hz tone at the voice rate.
	const uint32_t voice_rate = SpeechProcessor::SPEECH_SETTING_VOICE_SAMPLE_RATE;
	const int frames = static_cast<int>(voice_rate / 5);
	PackedFloat32Array mono;
	mono.resize(frames);
	for (int i = 0; i < frames; ++i) {
		const double t = static_cast<double>(i) / static_cast<double>(voice_rate);
		mono.write[i] = static_cast<float>(std::sin(2.0 * 3.14159265358979 * 440.0 * t) * 0.5);
	}
	processor.test_process_mono_audio_frames(mono, voice_rate);

	// Drain the middleware.
	const double drain_until = clock_ms + profile.base_latency_ms + 5.0 * profile.jitter_sigma_ms + 10.0;
	auto inflight = net.drain(drain_until);
	for (size_t i = 0; i < inflight.size(); ++i) {
		speech.on_received_audio_packet(peer_id, inflight[i].sequence_id,
				inflight[i].packet);
	}

	Dictionary elem = speech.get_player_audio()[peer_id];
	Array jb = elem["jitter_buffer"];

	// === Kernel-vs-engine agreement, observer style ===========
	// FramingCursor predicts how many packets test_process emits
	// from `frames` mono samples at the voice rate (no resample):
	//   expected_packets = frames / SPEECH_SETTING_BUFFER_FRAME_COUNT
	const int kernel_expected_sent =
			frames / SpeechProcessor::SPEECH_SETTING_BUFFER_FRAME_COUNT;
	const bool framing_agrees = (net.sent == kernel_expected_sent);

	// JitterAppend predicts: the jitter buffer grows by 1 per
	// in-order packet, capped at MAX_JITTER_BUFFER_SIZE. With
	// drops, fewer "filler" entries are needed (engine drops them
	// on receive). The exact size depends on the seed-dependent
	// reorder + drop sequence, but the kernel-style bound is:
	//   jb_size <= min(delivered + dropped, MAX_JITTER_BUFFER_SIZE)
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

	{
		NetworkProfile p;
		p.base_latency_ms = 5.0;
		p.seed = 0x1ABC0DE'001u;
		auto r = run_profile("lan (5 ms RTT)", p);
		render(r.snapshot);
		if (!r.engine_packet_count_matches_kernel || !r.engine_jb_size_matches_kernel) {
			std::fprintf(stderr, "FAIL: lan kernel ⇔ engine disagreement\n");
			++fails;
		}
	}

	{
		NetworkProfile p;
		p.base_latency_ms = 80.0;
		p.jitter_sigma_ms = 8.0;
		p.reorder_window = 3;
		p.reorder_prob = 0.15;
		p.seed = 0x2EA15E'002u;
		auto r = run_profile("wan (80 ms RTT, 8 ms σ)", p);
		render(r.snapshot);
		if (!r.engine_packet_count_matches_kernel || !r.engine_jb_size_matches_kernel) {
			std::fprintf(stderr, "FAIL: wan kernel ⇔ engine disagreement\n");
			++fails;
		}
	}

	{
		NetworkProfile p;
		p.base_latency_ms = 80.0;
		p.jitter_sigma_ms = 8.0;
		p.loss_prob = 0.10;
		p.reorder_window = 3;
		p.reorder_prob = 0.10;
		p.seed = 0x3105500'003u;
		auto r = run_profile("lossy (80 ms RTT, 8 ms σ, 10% loss)", p);
		render(r.snapshot);
		if (!r.engine_packet_count_matches_kernel || !r.engine_jb_size_matches_kernel) {
			std::fprintf(stderr, "FAIL: lossy kernel ⇔ engine disagreement\n");
			++fails;
		}
	}

	if (fails == 0) {
		std::printf("observer: PASS — kernel ⇔ engine agreement holds across all profiles\n");
		return 0;
	}
	std::fprintf(stderr, "observer: %d FAIL\n", fails);
	return 1;
}
