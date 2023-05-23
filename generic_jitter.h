#pragma once

#include "core/object/ref_counted.h"
#include "core/templates/ring_buffer.h"
#include "core/variant/variant.h"

// Based on speex's jitter buffer.

struct JitterBufferPacket {
	PackedByteArray data; // Pointer to the packet data
	int len = 0; // Length of the packet data
	int timestamp = 0; // Timestamp associated with the packet
	int span = 0; // Frame size or duration of the packet

	// You can add more fields if needed, depending on your specific use case
};

// GenericJitter: Adaptive jitter buffer for any type of data
// This is the jitter buffer that reorders packets and adjusts the buffer size
// to maintain good quality and low latency. It can handle any type of data such as audio, video, or animation.
class GenericJitter : public RefCounted {
	GDCLASS(GenericJitter, RefCounted);

private:
	// Generic jitter-buffer state. Never use it directly!
	uint8_t *current_packet; // Current packet data
	int valid_data; // True if packet data is valid
	int32_t frame_size; // Frame size of the decoder
	RingBuffer<JitterBufferPacket> ring_buffer = RingBuffer<JitterBufferPacket>(8);
	// Initialize the RingBuffer with a power of 2 size (e.g., 8 for a buffer size of 256)

	int decode(uint8_t *packet, uint8_t *out);

public:
	void generic_jitter_init(int frame_size);
	~GenericJitter();
	void generic_jitter_put(PackedByteArray packet, int len, int timestamp);
	PackedByteArray generic_jitter_get();
	int generic_jitter_get_pointer_timestamp();

protected:
	static void _bind_methods();
};
