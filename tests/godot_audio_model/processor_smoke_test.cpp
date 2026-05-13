// CHI-101 Phase A pass-7b smoke test — constructs a real
// SpeechProcessor (from godot-speech/speech_processor.cpp) linked
// against the audio model, drives a synthetic 480-sample voice
// packet through `encode_buffer`, and verifies the output is a
// nonzero Opus-compressed buffer.
//
// Doesn't exercise the full _mix_audio capture loop (that needs an
// AudioEffectCapture + AudioServer + AudioStreamPlayer wired up;
// pass-7c will land that). The encode-buffer path here proves
// the Opus encoder side links and runs through the wrapper code
// godot-speech writes.

#include "../../speech_processor.h"

#include <cmath>
#include <cstdio>

int main() {
	SpeechProcessor processor;
	std::printf("processor_smoke: SpeechProcessor constructed\n");

	// Build a 480-sample (10 ms @ 48 kHz) PCM buffer with a soft tone.
	// PCM is int16 stereo-like (single mono channel for this test).
	PackedByteArray pcm_in;
	const int frame_count = SpeechProcessor::SPEECH_SETTING_BUFFER_FRAME_COUNT;
	pcm_in.resize(frame_count * static_cast<int>(SpeechProcessor::SPEECH_SETTING_BUFFER_BYTE_COUNT));

	int16_t *pcm_ptr = reinterpret_cast<int16_t *>(pcm_in.ptrw());
	for (int i = 0; i < frame_count; ++i) {
		// 440 Hz tone at -12 dBFS — well within the encoder's range.
		const double t = static_cast<double>(i) / static_cast<double>(SpeechProcessor::SPEECH_SETTING_SAMPLE_RATE);
		pcm_ptr[i] = static_cast<int16_t>(std::sin(2.0 * 3.14159265358979 * 440.0 * t) * 8192.0);
	}

	PackedByteArray compressed;
	compressed.resize(SpeechProcessor::SPEECH_SETTING_INTERNAL_BUFFER_SIZE);
	int written = processor.encode_buffer(&pcm_in, &compressed);
	std::printf("processor_smoke: encode_buffer wrote %d bytes (negative = Opus error)\n",
			written);

	if (written <= 0) {
		std::fprintf(stderr, "FAIL: encode_buffer returned %d on a 440 Hz tone\n", written);
		return 1;
	}
	std::printf("processor_smoke: PASS — Opus emitted %d-byte compressed packet\n", written);

	// Round-trip through the decoder. Construct a SpeechDecoder via
	// the processor's get_speech_decoder factory; engine path
	// allocates the OpusDecoder.
	Ref<SpeechDecoder> decoder = processor.get_speech_decoder();
	if (decoder.is_null()) {
		std::fprintf(stderr, "FAIL: get_speech_decoder returned null Ref\n");
		return 1;
	}

	PackedByteArray pcm_out;
	pcm_out.resize(frame_count * static_cast<int>(SpeechProcessor::SPEECH_SETTING_BUFFER_BYTE_COUNT));
	int32_t decode_result = decoder->process(&compressed, &pcm_out,
			written, pcm_out.size(), frame_count);
	std::printf("processor_smoke: decoder->process returned %d (positive = frames decoded)\n",
			decode_result);

	if (decode_result != frame_count) {
		std::fprintf(stderr, "FAIL: decoder returned %d frames, expected %d\n",
				decode_result, frame_count);
		return 1;
	}

	std::printf("processor_smoke: PASS — Opus encode -> decode round-trip ran end-to-end\n");
	return 0;
}
