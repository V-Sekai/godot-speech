// CHI-101 Phase A — kernel-vs-runtime agreement test.
//
// The slang_validate kernels are normative specs for the
// engine's math layer. They're bit-exact-verified against a
// hand-written CPU reference in tests/slang_validate/. This test
// closes the other half: drive deterministic fixtures through the
// real godot-speech engine code (linked against the audio model)
// and assert the engine's observable output matches what each
// kernel's formula predicts.
//
// If any of these assertions fail, either:
//   * the engine has drifted from the spec (an F-finding like F1)
//   * the spec has drifted from the engine (revisit decision doc)
//
// Either way it's worth a kernel-vs-runtime divergence note.
//
// Covered:
//   FramingCursor — frames_emitted = total / frameSize
//   JitterAppend — append_new / filler_count for first packet,
//                  in-order, gap, overflow MAX_JITTER_BUFFER_SIZE
//
// VAD gating is Phase B (no voice path runs the VadGate kernel
// yet); FrameEnergy is per-frame energy that the engine doesn't
// compute — those agreement tests come once the engine integrates
// the energy/VAD signal path.

#include "../../speech.h"
#include "../../speech_processor.h"
#include "audio/audio_effect_capture.h"
#include "audio/audio_server.h"
#include "audio/audio_stream_generator.h"
#include "audio/audio_stream_player.h"
#include "core/core_types.h"

#include <cmath>
#include <cstdio>
#include <vector>

static int test_framing_cursor_agreement() {
	// Drive the engine capture path at the no-resample rate so the
	// only framing math being exercised is the integer divmod
	// FramingCursor encodes. SpeechProcessor::test_process_mono_audio_frames
	// at input_rate = 48000 makes libsamplerate a passthrough.
	//
	// For each input length N (mono samples), expected
	// packets_emitted = N / 480 (FramingCursor formula).
	const uint32_t voice_rate = SpeechProcessor::SPEECH_SETTING_VOICE_SAMPLE_RATE;
	const int frame_size = SpeechProcessor::SPEECH_SETTING_BUFFER_FRAME_COUNT; // 480

	struct Case {
		int input_samples;
		int expected_packets_kernel; // FramingCursor: input / 480
		const char *label;
	};

	const Case cases[] = {
		// underflow zone (engine F1 also emits 0)
		{ 0, 0, "0 samples" },
		{ 100, 0, "100 samples (< 480)" },
		// exact boundaries (F1 pre-fix dropped these; F1-fixed engine emits 1+)
		{ 480, 1, "exactly 480 (1 packet)" },
		{ 960, 2, "exactly 960 (2 packets)" },
		{ 1440, 3, "exactly 1440 (3 packets)" },
		// off-boundary
		{ 500, 1, "500 samples" },
		{ 999, 2, "999 samples" },
		{ 4800, 10, "100 ms at 48 kHz (10 packets)" },
	};

	int fails = 0;
	for (const auto &c : cases) {
		SpeechProcessor processor;
		std::vector<PackedByteArray> emitted;
		processor.register_speech_processed(
				[&emitted](SpeechProcessor::SpeechInput *input) {
					if (input && input->pcm_byte_array) {
						emitted.push_back(*input->pcm_byte_array);
					}
				});

		// Fill with audible content so the test_process loop's energy
		// gate doesn't reject packets. (Silence would be dropped
		// before reaching the speech_processed callback.)
		PackedFloat32Array mono;
		mono.resize(c.input_samples);
		for (int i = 0; i < c.input_samples; ++i) {
			const double t = static_cast<double>(i) / static_cast<double>(voice_rate);
			mono.write[i] = static_cast<float>(std::sin(2.0 * 3.14159265358979 * 440.0 * t) * 0.5);
		}
		processor.test_process_mono_audio_frames(mono, voice_rate);

		const int observed = static_cast<int>(emitted.size());
		const bool agree = (observed == c.expected_packets_kernel);
		std::printf("  framing  %-32s engine=%2d  kernel=%2d  %s\n",
				c.label, observed, c.expected_packets_kernel,
				agree ? "AGREE" : "DIFF");

		if (!agree) {
			std::fprintf(stderr,
					"DIFF: %s — engine emitted %d packets, FramingCursor predicts %d\n",
					c.label, observed, c.expected_packets_kernel);
			++fails;
		}
		(void)frame_size;
	}

	if (fails == 0) {
		std::printf("framing_cursor: kernel ⇔ engine agreement OK across %zu cases\n",
				sizeof(cases) / sizeof(cases[0]));
	}
	return fails;
}

static int test_jitter_append_agreement() {
	// Drive Speech::on_received_audio_packet with a hand-crafted
	// sequence and check the jitter_buffer's observable state
	// against what JitterAppend predicts.
	//
	// JitterAppend policy (per-event, given current {seq, seq_valid, size}):
	//   - first packet (seq_valid=0):
	//       filler=0, append=1, size: 0 -> 1
	//   - forward (incomingSeq > currentSeq):
	//       offset = inc - cur
	//       filler = offset - 1
	//       append = 1
	//       sizeAfter = currentSize + filler + 1
	//       drop = max(0, sizeAfter - maxSize)
	//       newSize = sizeAfter - drop

	// Set up player_audio entry for peer 1.
	Speech speech;
	// Read the engine's current MAX, not a hard-coded value. F4
	// raised the default from 16 to 32 to mitigate the C5/G181
	// PredictiveBVH gap class — the agreement test must track
	// whatever the engine currently uses.
	const int MAX = speech.get_max_jitter_buffer_size();
	std::printf("  jitter   engine MAX_JITTER_BUFFER_SIZE = %d\n", MAX);

	AudioStreamPlayer player;
	player.set_name(String("AudioStreamPlayer"));
	Ref<AudioStreamGenerator> gen(new AudioStreamGenerator());
	gen->set_mix_rate(48000.0f);
	gen->set_buffer_length(0.5f);
	player.set_stream(gen);
	speech.add_player_audio(1, &player);

	auto buf_size = [&]() -> int {
		Dictionary elem = speech.get_player_audio()[1];
		Array jb = elem["jitter_buffer"];
		return jb.size();
	};

	auto send = [&](int seq) {
		PackedByteArray p;
		p.resize(64);
		speech.on_received_audio_packet(/*peer*/ 1, seq, p);
	};

	struct Case {
		int seq;
		int expected_size_after;
		const char *label;
	};

	// JitterAppend predicts size after each event. First packet on
	// fresh peer: seq_valid=0 path → +1. Forward gap of N: +N (or
	// capped at MAX with dropFromFront).
	const Case cases[] = {
		{ 100, 1, "first packet (seq=100; init seq=99)" },
		{ 101, 2, "in-order seq=101" },
		{ 103, 4, "gap of 2 (filler=1 + 1)" }, // 2 + 2 = 4
		{ 100 + MAX + 10, MAX, "huge gap → clamp to MAX_JITTER_BUFFER_SIZE" },
		{ 100 + MAX + 11, MAX, "next in-order at full → still MAX (drops oldest)" },
	};

	int fails = 0;
	for (const auto &c : cases) {
		send(c.seq);
		const int observed = buf_size();
		const bool agree = (observed == c.expected_size_after);
		std::printf("  jitter   %-44s engine_size=%2d  kernel_pred=%2d  %s\n",
				c.label, observed, c.expected_size_after,
				agree ? "AGREE" : "DIFF");
		if (!agree) {
			std::fprintf(stderr,
					"DIFF: %s — engine jitter_buffer.size=%d, JitterAppend predicts %d\n",
					c.label, observed, c.expected_size_after);
			++fails;
		}
	}

	if (fails == 0) {
		std::printf("jitter_append: kernel ⇔ engine agreement OK across %zu cases\n",
				sizeof(cases) / sizeof(cases[0]));
	}
	return fails;
}

int main() {
	std::printf("agreement: kernel-vs-runtime equivalence test\n");
	int fails = 0;
	fails += test_framing_cursor_agreement();
	fails += test_jitter_append_agreement();
	if (fails == 0) {
		std::printf("agreement: PASS — engine matches slang_validate kernels across all cases\n");
		return 0;
	}
	std::fprintf(stderr, "agreement: %d FAIL\n", fails);
	return 1;
}
