#include "generic_jitter.h"

int GenericJitter::decode(uint8_t *packet, uint8_t *out) {
	// Implement your decoding logic here
	// For now, let's return 0 as a placeholder
	return 0;
}

void GenericJitter::generic_jitter_init(int frame_size) {
	this->frame_size = frame_size;

	current_packet = nullptr;
	valid_data = 0;
}

GenericJitter::~GenericJitter() {
	if (current_packet != nullptr) {
		free(current_packet);
	}
}

void GenericJitter::generic_jitter_put(PackedByteArray packet, int len, int timestamp) {
	JitterBufferPacket p;
	p.data = packet;
	p.len = len;
	p.timestamp = timestamp;
	p.span = frame_size;
	ring_buffer.write(p);
}

PackedByteArray GenericJitter::generic_jitter_get() {
	int ret = OK;
	PackedByteArray data;
	JitterBufferPacket packet;
	packet.len = 2048;
	data.resize(packet.len);
	data.fill(0);
	packet.data = data;

	PackedByteArray out;

	if (valid_data) {
		// Try decoding last received packet
		ret = decode(current_packet, out.ptrw());
		if (ret == 0) {
			ring_buffer.advance_read(1);
			return PackedByteArray();
		} else {
			valid_data = 0;
		}
	}

	if (!ring_buffer.read(&packet, sizeof(JitterBufferPacket))) {
		// No packet found
		decode(nullptr, out.ptrw());
	} else {
		// Decode packet
		ret = decode(packet.data.ptrw(), out.ptrw());
		if (ret == 0) {
			valid_data = 1;
			if (current_packet != nullptr) {
				free(current_packet);
			}
			current_packet = static_cast<uint8_t *>(malloc(packet.len));
			memcpy(current_packet, packet.data.ptrw(), packet.len);
		} else {
			// Error while decoding
			memset(out.ptrw(), 0, frame_size);
		}
	}

	ring_buffer.advance_read(1);
}

int GenericJitter::generic_jitter_get_pointer_timestamp() {
	// Return the timestamp of the current read position in the ring buffer
	JitterBufferPacket packet;
	RingBuffer temp_ring_buffer = ring_buffer; // Create a temporary copy of the ring buffer

	if (temp_ring_buffer.read(&packet, sizeof(JitterBufferPacket), false)) {
		return packet.timestamp;
	} else {
		return -1; // No packet available at the current read position
	}
}

void GenericJitter::_bind_methods() {
	ClassDB::bind_method(D_METHOD("generic_jitter_init", "decoder", "frame_size"), &GenericJitter::generic_jitter_init);
	ClassDB::bind_method(D_METHOD("generic_jitter_put", "packet", "len", "timestamp"), &GenericJitter::generic_jitter_put);
	ClassDB::bind_method(D_METHOD("generic_jitter_get", "current_timestamp"), &GenericJitter::generic_jitter_get);
	ClassDB::bind_method(D_METHOD("generic_jitter_get_pointer_timestamp"), &GenericJitter::generic_jitter_get_pointer_timestamp);
}
