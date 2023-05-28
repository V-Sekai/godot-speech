
#ifndef TEST_SPEECH_H
#define TEST_SPEECH_H

#include "core/variant/variant.h"
#include "tests/test_macros.h"

#include "modules/speech/jitter.h"

namespace TestJitter {
TEST_CASE("[Modules][Speech] Basic Tests") {
	{
		int step_size = 10;
		Ref<JitterBuffer> jitter;
		jitter.instantiate();
		Ref<VoipJitterBuffer> buffer;
		buffer.instantiate();
		jitter = buffer->jitter_buffer_init(step_size);
		CHECK_MESSAGE(jitter.is_valid(), "Initialize the jitter buffer");
		buffer->jitter_buffer_destroy(jitter);
		CHECK_MESSAGE(jitter->get_reset_state() == 1, "Destroy the jitter buffer.");
	}
	{
		int step_size = 10;
		Ref<JitterBuffer> jitter;
		jitter.instantiate();
		Ref<VoipJitterBuffer> buffer;
		buffer.instantiate();
		jitter = buffer->jitter_buffer_init(step_size);
		CHECK_MESSAGE(jitter.is_valid(), "Initialize the jitter buffer");
		buffer->jitter_buffer_reset(jitter);
		CHECK_MESSAGE(jitter->get_reset_state() == 1, "Reset the jitter buffer.");
		buffer->jitter_buffer_destroy(jitter);
		CHECK_MESSAGE(jitter->get_reset_state() == 1, "Destroy the jitter buffer.");
	}
	{
		int step_size = 10;
		Ref<JitterBuffer> jitter;
		jitter.instantiate();
		Ref<VoipJitterBuffer> buffer;
		buffer.instantiate();
		jitter = buffer->jitter_buffer_init(step_size);
		CHECK_MESSAGE(jitter.is_valid(), "Initialize the jitter buffer");
		int request = JITTER_BUFFER_SET_MARGIN;
		int32_t margin = 5;
		int result = buffer->jitter_buffer_ctl(jitter, request, &margin);
		CHECK_MESSAGE(result == 0, "Set jitter buffer margin");
		buffer->jitter_buffer_destroy(jitter);
		CHECK_MESSAGE(jitter->get_reset_state() == 1, "Reset the jitter buffer.");
	}
	{
		int step_size = 10;
		Ref<JitterBuffer> jitter;
		jitter.instantiate();
		Ref<VoipJitterBuffer> buffer;
		buffer.instantiate();
		jitter = buffer->jitter_buffer_init(step_size);
		CHECK_MESSAGE(jitter.is_valid(), "Initialize the jitter buffer");
		Ref<JitterBufferPacket> packet;
		packet.instantiate();
		PackedByteArray bytes;
		bytes.resize(100);
		bytes.fill(0);
		packet->set_data(bytes);
		buffer->jitter_buffer_put(jitter, packet);
		Ref<JitterBufferPacket> retrieved_packet;
		retrieved_packet.instantiate();
		retrieved_packet->set_data(bytes);
		buffer->jitter_buffer_put(jitter, retrieved_packet);
		int32_t desired_span = 10;
		Array result = buffer->jitter_buffer_get(jitter, retrieved_packet, desired_span);
		CHECK_MESSAGE(result.size(), "Get the jitter buffer.");
		buffer->jitter_buffer_tick(jitter);
		CHECK_MESSAGE(result.size(), "Tick the jitter buffer.");
		buffer->jitter_buffer_destroy(jitter);
		CHECK_MESSAGE(jitter->get_reset_state() == 1, "Destroy the jitter buffer.");
	}
	{
		int step_size = 10;
		Ref<JitterBuffer> jitter;
		jitter.instantiate();
		Ref<VoipJitterBuffer> buffer;
		buffer.instantiate();
		jitter = buffer->jitter_buffer_init(step_size);
		Ref<JitterBufferPacket> packet;
		packet.instantiate();
		buffer->jitter_buffer_put(jitter, packet);
		Ref<JitterBufferPacket> another_packet;
		another_packet.instantiate();
		int result = buffer->jitter_buffer_get_another(jitter, another_packet);
		CHECK_MESSAGE(result, "Retrieved packet");
		buffer->jitter_buffer_destroy(jitter);
		CHECK_MESSAGE(jitter->get_reset_state() == 1, "Destroy the jitter buffer.");
	}
	{
		int step_size = 10;
		Ref<JitterBuffer> jitter;
		jitter.instantiate();
		Ref<VoipJitterBuffer> buffer;
		buffer.instantiate();
		jitter = buffer->jitter_buffer_init(step_size);
		Ref<JitterBufferPacket> packet;
		packet.instantiate();
		PackedByteArray bytes;
		bytes.resize(100);
		bytes.fill(0);
		packet->set_data(bytes);
		int32_t delay = buffer->jitter_buffer_update_delay(jitter, packet);
		CHECK_MESSAGE(delay == 0, "The retrieved packet.");
		buffer->jitter_buffer_destroy(jitter);
		CHECK_MESSAGE(jitter->get_reset_state() == 1, "Destroy the jitter buffer.");
	}
	{
		int step_size = 10;
		Ref<JitterBuffer> jitter;
		jitter.instantiate();
		Ref<VoipJitterBuffer> buffer;
		buffer.instantiate();
		jitter = buffer->jitter_buffer_init(step_size);
		int timestamp = buffer->jitter_buffer_get_pointer_timestamp(jitter);
		CHECK_MESSAGE(timestamp == 0, "Got a timestamp.");
		buffer->jitter_buffer_destroy(jitter);
		CHECK_MESSAGE(jitter->get_reset_state() == 1, "Destroy the jitter buffer.");
	}
}
} // namespace TestJitter

#endif // TEST_SPEECH_H
