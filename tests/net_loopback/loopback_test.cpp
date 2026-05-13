// CHI-101 Phase A — net_loopback step 1: assertion-based driver.
//
// Sender (SpeechProcessor::test_process_mono_audio_frames)
//   -> speech_processed callback
//   -> NetMiddleware (synthetic jitter / loss / reorder)
//   -> Speech::on_received_audio_packet
//   -> jitter buffer
//
// The middleware advances a synthetic clock; the test runs
// multiple network profiles back-to-back and asserts the engine
// behaves sensibly under each:
//
//   "lan"      — 5 ms latency, 0% loss, no jitter: every packet
//                delivered, no skips.
//   "wan"      — 80 ms latency, 8 ms jitter σ, 0% loss:
//                delivery order may shift but no packets lost.
//   "lossy"    — 80 ms latency, 8 ms jitter, 10% loss: some
//                packets drop; jitter buffer fills with valid +
//                filler entries.
//
// Each profile gets its own clean Speech/SpeechProcessor pair.

#include "middleware.h"

#include "../../speech.h"
#include "../../speech_processor.h"
#include "audio/audio_stream_generator.h"
#include "audio/audio_stream_player.h"
#include "core/core_types.h"

#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

struct ProfileResult {
	int sent = 0;
	int dropped = 0;
	int reordered = 0;
	int delivered = 0;
	int jitter_buffer_size_end = 0;
};

static ProfileResult run_profile(const char *label,
		const NetworkProfile &profile) {
	std::printf("loopback: profile=%-8s  base=%6.1f ms  σ=%5.1f ms  loss=%.0f%%  reorder=%.0f%%\n",
			label, profile.base_latency_ms, profile.jitter_sigma_ms,
			profile.loss_prob * 100.0, profile.reorder_prob * 100.0);

	NetMiddleware net(profile);

	// Receiver side.
	Speech speech;
	const int peer_id = 1;
	AudioStreamPlayer player;
	player.set_name(String("AudioStreamPlayer"));
	Ref<AudioStreamGenerator> gen(new AudioStreamGenerator());
	gen->set_mix_rate(48000.0f);
	gen->set_buffer_length(1.0f);
	player.set_stream(gen);
	speech.add_player_audio(peer_id, &player);

	// Sender side. test_process_mono_audio_frames emits one
	// speech_processed callback per 10 ms packet — push each one
	// through the middleware with the synthetic clock at the
	// packet's nominal capture time.
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
	const int frames = static_cast<int>(voice_rate / 5); // 200 ms
	PackedFloat32Array mono;
	mono.resize(frames);
	for (int i = 0; i < frames; ++i) {
		const double t = static_cast<double>(i) / static_cast<double>(voice_rate);
		mono.write[i] = static_cast<float>(std::sin(2.0 * 3.14159265358979 * 440.0 * t) * 0.5);
	}
	processor.test_process_mono_audio_frames(mono, voice_rate);

	// Drain the middleware. Advance the synthetic clock past the
	// last sent packet + max expected delay so every queued
	// packet hits the receiver.
	const double drain_until = clock_ms + profile.base_latency_ms + 5.0 * profile.jitter_sigma_ms + 10.0;
	auto inflight = net.drain(drain_until);
	for (size_t i = 0; i < inflight.size(); ++i) {
		speech.on_received_audio_packet(peer_id, inflight[i].sequence_id,
				inflight[i].packet);
	}

	// Inspect end state.
	Dictionary elem = speech.get_player_audio()[peer_id];
	Array jb = elem["jitter_buffer"];

	ProfileResult r;
	r.sent = net.sent;
	r.dropped = net.dropped;
	r.reordered = net.reordered;
	r.delivered = net.delivered;
	r.jitter_buffer_size_end = jb.size();

	std::printf("  sent=%-3d delivered=%-3d dropped=%-3d reordered=%-3d  jb_size=%d\n",
			r.sent, r.delivered, r.dropped, r.reordered,
			r.jitter_buffer_size_end);
	return r;
}

int main() {
	int fails = 0;

	// LAN: deterministic delivery, every packet arrives, no drops.
	{
		NetworkProfile p;
		p.base_latency_ms = 5.0;
		p.seed = 0x1ABC0DE'001u;
		auto r = run_profile("lan", p);
		if (r.delivered != r.sent || r.dropped != 0) {
			std::fprintf(stderr,
					"FAIL: lan profile lost packets (sent=%d, delivered=%d, dropped=%d)\n",
					r.sent, r.delivered, r.dropped);
			++fails;
		}
		if (r.jitter_buffer_size_end <= 0) {
			std::fprintf(stderr,
					"FAIL: lan profile left jitter buffer empty (sent %d packets)\n",
					r.sent);
			++fails;
		}
	}

	// WAN: jitter shouldn't drop anything, just rearrange arrival.
	{
		NetworkProfile p;
		p.base_latency_ms = 80.0;
		p.jitter_sigma_ms = 8.0;
		p.reorder_window = 3;
		p.reorder_prob = 0.15;
		p.seed = 0x2EA15E'002u;
		auto r = run_profile("wan", p);
		if (r.delivered != r.sent || r.dropped != 0) {
			std::fprintf(stderr,
					"FAIL: wan profile lost packets (sent=%d, delivered=%d, dropped=%d)\n",
					r.sent, r.delivered, r.dropped);
			++fails;
		}
		if (r.reordered == 0) {
			std::fprintf(stderr,
					"FAIL: wan profile saw zero reorders with reorder_prob=15%% over %d packets\n",
					r.sent);
			++fails;
		}
	}

	// Lossy: ~10% drop rate; engine must absorb it via filler
	// packets and not crash.
	{
		NetworkProfile p;
		p.base_latency_ms = 80.0;
		p.jitter_sigma_ms = 8.0;
		p.loss_prob = 0.10;
		p.reorder_window = 3;
		p.reorder_prob = 0.10;
		p.seed = 0x3105500'003u;
		auto r = run_profile("lossy", p);
		if (r.dropped == 0) {
			std::fprintf(stderr,
					"FAIL: lossy profile dropped zero packets at 10%% loss over %d sent\n",
					r.sent);
			++fails;
		}
		if (r.delivered + r.dropped != r.sent) {
			std::fprintf(stderr,
					"FAIL: lossy profile bookkeeping: sent=%d, delivered=%d, dropped=%d\n",
					r.sent, r.delivered, r.dropped);
			++fails;
		}
		if (r.jitter_buffer_size_end <= 0) {
			std::fprintf(stderr,
					"FAIL: lossy profile left jitter buffer empty\n");
			++fails;
		}
	}

	if (fails == 0) {
		std::printf("loopback: PASS — all three profiles delivered the expected sender->receiver behaviour\n");
		return 0;
	}
	std::fprintf(stderr, "loopback: %d FAIL\n", fails);
	return 1;
}
