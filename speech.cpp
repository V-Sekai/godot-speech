/*************************************************************************/
/*  speech.cpp                                                           */
/*************************************************************************/
/*                       This file is part of:                           */
/*                           GODOT ENGINE                                */
/*                      https://godotengine.org                          */
/*************************************************************************/
/* Copyright (c) 2007-2021 Juan Linietsky, Ariel Manzur.                 */
/* Copyright (c) 2014-2021 Godot Engine contributors (cf. AUTHORS.md).   */
/*                                                                       */
/* Permission is hereby granted, free of charge, to any person obtaining */
/* a copy of this software and associated documentation files (the       */
/* "Software"), to deal in the Software without restriction, including   */
/* without limitation the rights to use, copy, modify, merge, publish,   */
/* distribute, sublicense, and/or sell copies of the Software, and to    */
/* permit persons to whom the Software is furnished to do so, subject to */
/* the following conditions:                                             */
/*                                                                       */
/* The above copyright notice and this permission notice shall be        */
/* included in all copies or substantial portions of the Software.       */
/*                                                                       */
/* THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,       */
/* EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF    */
/* MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.*/
/* IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY  */
/* CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,  */
/* TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE     */
/* SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.                */
/*************************************************************************/

#include "speech.h"

void Speech::preallocate_buffers() {
	input_byte_array.resize(SpeechProcessor::SPEECH_SETTING_PCM_BUFFER_SIZE);
	input_byte_array.fill(0);
	compression_output_byte_array.resize(
			SpeechProcessor::SPEECH_SETTING_PCM_BUFFER_SIZE);
	compression_output_byte_array.fill(0);
	for (int i = 0; i < MAX_AUDIO_BUFFER_ARRAY_SIZE; i++) {
		input_audio_buffer_array[i].compressed_byte_array.resize(
				SpeechProcessor::SPEECH_SETTING_PCM_BUFFER_SIZE);
		input_audio_buffer_array[i].compressed_byte_array.fill(0);
	}
}

void Speech::setup_connections() {
	if (speech_processor) {
		speech_processor->register_speech_processed(
				std::function<void(SpeechProcessor::SpeechInput *)>(std::bind(
						&Speech::speech_processed, this, std::placeholders::_1)));
	}
}

Speech::InputPacket *Speech::get_next_valid_input_packet() {
	if (current_input_size < MAX_AUDIO_BUFFER_ARRAY_SIZE) {
		InputPacket *input_packet = &input_audio_buffer_array[current_input_size];
		current_input_size++;
		return input_packet;
	} else {
		for (int i = MAX_AUDIO_BUFFER_ARRAY_SIZE - 1; i > 0; i--) {
			memcpy(input_audio_buffer_array[i - 1].compressed_byte_array.ptrw(),
					input_audio_buffer_array[i].compressed_byte_array.ptr(),
					SpeechProcessor::SPEECH_SETTING_PCM_BUFFER_SIZE);

			input_audio_buffer_array[i - 1].buffer_size =
					input_audio_buffer_array[i].buffer_size;
			input_audio_buffer_array[i - 1].loudness =
					input_audio_buffer_array[i].loudness;
		}
		skipped_audio_packets++;
		return &input_audio_buffer_array[MAX_AUDIO_BUFFER_ARRAY_SIZE - 1];
	}
}

void Speech::speech_processed(SpeechProcessor::SpeechInput *p_mic_input) {
	// Copy the raw PCM data from the SpeechInput packet to the input byte array
	PackedByteArray *mic_input_byte_array = p_mic_input->pcm_byte_array;
	memcpy(input_byte_array.ptrw(), mic_input_byte_array->ptr(),
			SpeechProcessor::SPEECH_SETTING_PCM_BUFFER_SIZE);

	// Create a new SpeechProcessor::CompressedBufferInput to be passed into the
	// compressor and assign it the compressed_byte_array from the input packet
	SpeechProcessor::CompressedSpeechBuffer compressed_buffer_input;
	compressed_buffer_input.compressed_byte_array =
			&compression_output_byte_array;

	// Compress the packet
	speech_processor->compress_buffer_internal(&input_byte_array,
			&compressed_buffer_input);
	{
		// Lock
		MutexLock mutex_lock(audio_mutex);

		int64_t size = compressed_buffer_input.buffer_size;
		ERR_FAIL_COND(size > SpeechProcessor::SPEECH_SETTING_PCM_BUFFER_SIZE);
		// Find the next valid input packet in the queue
		InputPacket *input_packet = get_next_valid_input_packet();
		// Copy the buffer size from the compressed_buffer_input back into the
		// input packet
		memcpy(input_packet->compressed_byte_array.ptrw(),
				compressed_buffer_input.compressed_byte_array->ptr(), size);

		input_packet->buffer_size = size;
		input_packet->loudness = p_mic_input->volume;
	}
}

int Speech::get_jitter_buffer_speedup() const {
	return JITTER_BUFFER_SPEEDUP;
}

void Speech::set_jitter_buffer_speedup(int p_jitter_buffer_speedup) {
	JITTER_BUFFER_SPEEDUP = p_jitter_buffer_speedup;
}

int Speech::get_jitter_buffer_slowdown() const {
	return JITTER_BUFFER_SLOWDOWN;
}

void Speech::set_jitter_buffer_slowdown(int p_jitter_buffer_slowdown) {
	JITTER_BUFFER_SLOWDOWN = p_jitter_buffer_slowdown;
}

float Speech::get_stream_speedup_pitch() const {
	return STREAM_SPEEDUP_PITCH;
}

void Speech::set_stream_speedup_pitch(float p_stream_speedup_pitch) {
	STREAM_SPEEDUP_PITCH = p_stream_speedup_pitch;
}

int Speech::get_max_jitter_buffer_size() const {
	return MAX_JITTER_BUFFER_SIZE;
}

void Speech::set_max_jitter_buffer_size(int p_max_jitter_buffer_size) {
	MAX_JITTER_BUFFER_SIZE = p_max_jitter_buffer_size;
}

float Speech::get_buffer_delay_threshold() const {
	return BUFFER_DELAY_THRESHOLD;
}

void Speech::set_buffer_delay_threshold(float p_buffer_delay_threshold) {
	BUFFER_DELAY_THRESHOLD = p_buffer_delay_threshold;
}

float Speech::get_stream_standard_pitch() const {
	return STREAM_STANDARD_PITCH;
}

void Speech::set_stream_standard_pitch(float p_stream_standard_pitch) {
	STREAM_STANDARD_PITCH = p_stream_standard_pitch;
}

bool Speech::get_debug() const {
	return DEBUG;
}

void Speech::set_debug(bool val) {
	DEBUG = val;
}

bool Speech::get_use_sample_stretching() const {
	return use_sample_stretching;
}

void Speech::set_use_sample_stretching(bool val) {
	use_sample_stretching = val;
}

PackedVector2Array Speech::get_uncompressed_audio() const {
	return uncompressed_audio;
}

void Speech::set_uncompressed_audio(PackedVector2Array val) {
	uncompressed_audio = val;
}

int Speech::get_packets_received_this_frame() const {
	return packets_received_this_frame;
}

void Speech::set_packets_received_this_frame(int val) {
	packets_received_this_frame = val;
}

int Speech::get_playback_ring_buffer_length() const {
	return playback_ring_buffer_length;
}

void Speech::set_playback_ring_buffer_length(int val) {
	playback_ring_buffer_length = val;
}

PackedVector2Array Speech::get_blank_packet() const {
	return blank_packet;
}

void Speech::set_blank_packet(PackedVector2Array val) {
	blank_packet = val;
}

Dictionary Speech::get_player_audio() const {
	return player_audio;
}

void Speech::set_player_audio(Dictionary val) {
	player_audio = val;
}

int Speech::nearest_shift(int p_number) {
	for (int32_t i = 30; i-- > 0;) {
		if (p_number & (1 << i)) {
			return i + 1;
		}
	}
	return 0;
}

int Speech::calc_playback_ring_buffer_length(Ref<AudioStreamGenerator> audio_stream_generator) {
	int target_buffer_size = int(audio_stream_generator->get_mix_rate() * audio_stream_generator->get_buffer_length());
	return (1 << nearest_shift(target_buffer_size));
}

void Speech::_bind_methods() {
	ClassDB::bind_method(D_METHOD("get_skipped_audio_packets"),
			&Speech::get_skipped_audio_packets);
	ClassDB::bind_method(D_METHOD("clear_skipped_audio_packets"),
			&Speech::clear_skipped_audio_packets);

	ClassDB::bind_method(D_METHOD("decompress_buffer", "decoder", "read_array",
								 "read_size", "write_array"),
			&Speech::decompress_buffer);

	ClassDB::bind_method(D_METHOD("copy_and_clear_buffers"),
			&Speech::copy_and_clear_buffers);
	ClassDB::bind_method(D_METHOD("get_speech_decoder"),
			&Speech::get_speech_decoder);
	ClassDB::bind_method(D_METHOD("get_stats"), &Speech::get_stats);

	ClassDB::bind_method(D_METHOD("start_recording"), &Speech::start_recording);
	ClassDB::bind_method(D_METHOD("end_recording"), &Speech::end_recording);

	ClassDB::bind_method(D_METHOD("set_streaming_bus", "bus"),
			&Speech::set_streaming_bus);
	ClassDB::bind_method(D_METHOD("set_audio_input_stream_player", "player"),
			&Speech::set_audio_input_stream_player);
	ClassDB::bind_method(D_METHOD("set_buffer_delay_threshold", "buffer_delay_threshold"),
			&Speech::set_buffer_delay_threshold);
	ClassDB::bind_method(D_METHOD("get_buffer_delay_threshold"),
			&Speech::get_buffer_delay_threshold);
	ClassDB::bind_method(D_METHOD("get_stream_standard_pitch"),
			&Speech::get_stream_standard_pitch);
	ClassDB::bind_method(D_METHOD("set_stream_standard_pitch", "stream_standard_pitch"),
			&Speech::set_stream_standard_pitch);
	ClassDB::bind_method(D_METHOD("get_stream_speedup_pitch"),
			&Speech::get_stream_standard_pitch);
	ClassDB::bind_method(D_METHOD("set_stream_speedup_pitch", "stream_speedup_pitch"),
			&Speech::set_stream_standard_pitch);
	ClassDB::bind_method(D_METHOD("get_max_jitter_buffer_size"),
			&Speech::get_max_jitter_buffer_size);
	ClassDB::bind_method(D_METHOD("set_max_jitter_buffer_size", "max_jitter_buffer_size"),
			&Speech::set_max_jitter_buffer_size);
	ClassDB::bind_method(D_METHOD("get_jitter_buffer_speedup"),
			&Speech::get_jitter_buffer_speedup);
	ClassDB::bind_method(D_METHOD("set_jitter_buffer_speedup", "jitter_buffer_speedup"),
			&Speech::set_jitter_buffer_speedup);
	ClassDB::bind_method(D_METHOD("get_jitter_buffer_slowdown"),
			&Speech::get_jitter_buffer_slowdown);
	ClassDB::bind_method(D_METHOD("set_jitter_buffer_slowdown", "jitter_buffer_slowdown"),
			&Speech::set_jitter_buffer_slowdown);
	ClassDB::bind_method(D_METHOD("get_debug"),
			&Speech::get_debug);
	ClassDB::bind_method(D_METHOD("set_debug", "debug"),
			&Speech::set_debug);
	ClassDB::bind_method(D_METHOD("get_debug"),
			&Speech::get_debug);
	ClassDB::bind_method(D_METHOD("set_debug", "debug"),
			&Speech::set_debug);
	ClassDB::bind_method(D_METHOD("get_uncompressed_audio"),
			&Speech::get_uncompressed_audio);
	ClassDB::bind_method(D_METHOD("set_uncompressed_audio", "uncompressed_audio"),
			&Speech::set_uncompressed_audio);
	ClassDB::bind_method(D_METHOD("get_packets_received_this_frame"),
			&Speech::get_packets_received_this_frame);
	ClassDB::bind_method(D_METHOD("set_packets_received_this_frame", "packets_received_this_frame"),
			&Speech::set_packets_received_this_frame);
	ClassDB::bind_method(D_METHOD("get_playback_ring_buffer_length"),
			&Speech::get_playback_ring_buffer_length);
	ClassDB::bind_method(D_METHOD("set_playback_ring_buffer_length", "playback_ring_buffer_length"),
			&Speech::set_playback_ring_buffer_length);
	ClassDB::bind_method(D_METHOD("get_blank_packet"),
			&Speech::get_blank_packet);
	ClassDB::bind_method(D_METHOD("set_blank_packet", "blank_packet"),
			&Speech::set_blank_packet);
	ClassDB::bind_method(D_METHOD("get_player_audio"),
			&Speech::get_player_audio);
	ClassDB::bind_method(D_METHOD("set_player_audio", "player_audio"),
			&Speech::set_player_audio);
	ClassDB::bind_method(D_METHOD("get_use_sample_stretching"),
			&Speech::get_use_sample_stretching);
	ClassDB::bind_method(D_METHOD("set_use_sample_stretching", "use_sample_stretching"),
			&Speech::set_use_sample_stretching);
	ClassDB::bind_method(D_METHOD("calc_playback_ring_buffer_length", "generator"),
			&Speech::calc_playback_ring_buffer_length);
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "BUFFER_DELAY_THRESHOLD"), "set_buffer_delay_threshold",
			"get_buffer_delay_threshold");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "STREAM_STANDARD_PITCH"), "set_stream_standard_pitch",
			"get_stream_standard_pitch");
	ADD_PROPERTY(PropertyInfo(Variant::INT, "MAX_JITTER_BUFFER_SIZE"), "set_max_jitter_buffer_size",
			"get_max_jitter_buffer_size");
	ADD_PROPERTY(PropertyInfo(Variant::INT, "STREAM_SPEEDUP_PITCH"), "set_stream_speedup_pitch",
			"get_stream_speedup_pitch");
	ADD_PROPERTY(PropertyInfo(Variant::INT, "JITTER_BUFFER_SLOWDOWN"), "set_jitter_buffer_slowdown",
			"get_jitter_buffer_slowdown");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "JITTER_BUFFER_SPEEDUP"), "set_jitter_buffer_speedup",
			"get_jitter_buffer_speedup");
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "DEBUG"), "set_debug",
			"get_debug");
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "use_sample_stretching"), "set_use_sample_stretching",
			"get_use_sample_stretching");
	ADD_PROPERTY(PropertyInfo(Variant::PACKED_VECTOR2_ARRAY, "uncompressed_audio", PROPERTY_HINT_NONE, "", PROPERTY_USAGE_NONE), "set_uncompressed_audio",
			"get_uncompressed_audio");
	ADD_PROPERTY(PropertyInfo(Variant::INT, "packets_received_this_frame"), "set_packets_received_this_frame",
			"get_packets_received_this_frame");
	ADD_PROPERTY(PropertyInfo(Variant::INT, "playback_ring_buffer_length"), "set_playback_ring_buffer_length",
			"get_playback_ring_buffer_length");
	ADD_PROPERTY(PropertyInfo(Variant::PACKED_VECTOR2_ARRAY, "blank_packet", PROPERTY_HINT_NONE, "", PROPERTY_USAGE_NONE), "set_blank_packet",
			"get_blank_packet");
	ADD_PROPERTY(PropertyInfo(Variant::DICTIONARY, "player_audio", PROPERTY_HINT_NONE, "", PROPERTY_USAGE_NONE), "set_player_audio",
			"get_player_audio");
}

int Speech::get_skipped_audio_packets() {
	return skipped_audio_packets;
}

void Speech::clear_skipped_audio_packets() {
	skipped_audio_packets = 0;
}

PackedVector2Array Speech::decompress_buffer(Ref<SpeechDecoder> p_speech_decoder, PackedByteArray p_read_byte_array, const int p_read_size, PackedVector2Array p_write_vec2_array) {
	if (p_read_byte_array.size() < p_read_size) {
		ERR_PRINT("SpeechDecoder: read byte_array size!");
		return PackedVector2Array();
	}

	if (speech_processor->decompress_buffer_internal(
				p_speech_decoder.ptr(), &p_read_byte_array, p_read_size,
				&p_write_vec2_array)) {
		return p_write_vec2_array;
	}

	return PackedVector2Array();
}

Array Speech::copy_and_clear_buffers() {
	MutexLock mutex_lock(audio_mutex);

	Array output_array;
	output_array.resize(current_input_size);

	for (int i = 0; i < current_input_size; i++) {
		Dictionary dict;

		dict["byte_array"] = input_audio_buffer_array[i].compressed_byte_array;
		dict["buffer_size"] = input_audio_buffer_array[i].buffer_size;
		dict["loudness"] = input_audio_buffer_array[i].loudness;

		output_array[i] = dict;
	}
	current_input_size = 0;

	return output_array;
}

Ref<SpeechDecoder> Speech::get_speech_decoder() {
	if (speech_processor) {
		return speech_processor->get_speech_decoder();
	} else {
		return nullptr;
	}
}

bool Speech::start_recording() {
	if (speech_processor) {
		speech_processor->start();
		skipped_audio_packets = 0;
		return true;
	}

	return false;
}

void Speech::end_recording() {
	if (speech_processor) {
		speech_processor->stop();
	}
	if (has_method("clear_all_player_audio")) {
		call("clear_all_player_audio");
	}
}

void Speech::_notification(int p_what) {
	switch (p_what) {
		case NOTIFICATION_READY: {
			setup_connections();
			if (speech_processor) {
				add_child(speech_processor, true);
				speech_processor->set_owner(get_owner());
			}
			uncompressed_audio.resize(
					SpeechProcessor::SPEECH_SETTING_BUFFER_FRAME_COUNT);
			uncompressed_audio.fill(Vector2());
			break;
		}
		case NOTIFICATION_POSTINITIALIZE: {
			blank_packet.resize(SpeechProcessor::SPEECH_SETTING_BUFFER_FRAME_COUNT);
			blank_packet.fill(Vector2());
			for (int32_t i = 0; i < SpeechProcessor::SPEECH_SETTING_BUFFER_FRAME_COUNT; i++) {
				blank_packet.write[i] = Vector2();
			}
			break;
		}
		default: {
			break;
		}
	}
}

void Speech::set_streaming_bus(const String &p_name) {
	if (speech_processor) {
		speech_processor->set_streaming_bus(p_name);
	}
}

bool Speech::set_audio_input_stream_player(Node *p_audio_stream) {
	AudioStreamPlayer *player = cast_to<AudioStreamPlayer>(p_audio_stream);
	ERR_FAIL_NULL_V(player, false);
	if (!speech_processor) {
		return false;
	}
	speech_processor->set_audio_input_stream_player(player);
	return true;
}

Dictionary Speech::get_stats() {
	if (speech_processor) {
		return speech_processor->get_stats();
	}
	return Dictionary();
}

Speech::Speech() {
	speech_processor = memnew(SpeechProcessor);
	preallocate_buffers();
}

Speech::~Speech() {
}
