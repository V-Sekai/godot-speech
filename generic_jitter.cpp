#include "generic_jitter.h"
#include "core/variant/variant.h"

Error GenericJitter::decode(const PackedByteArray &packet, PackedByteArray &out) {
	// Implement your decoding logic here
	// For now, let's return 0 as a placeholder
	return OK;
}

void GenericJitter::generic_jitter_init(int frame_size) {
	this->frame_size = frame_size;
	current_packet.clear();
	valid_data = 0;
}

GenericJitter::~GenericJitter() {
}

void GenericJitter::generic_jitter_put(PackedByteArray packet, int len, int timestamp) {
	JitterBufferPacket p;
	
	p.data = packet;
	p.timestamp = timestamp;
	p.span = frame_size;
	ring_buffer.write(p);
}

PackedByteArray GenericJitter::generic_jitter_get(Dictionary r_metadata) {
	int ret = OK;
	JitterBufferPacket packet;
	PackedByteArray data;
	data.resize(2048);
	data.fill(0);
	packet.data = data;

	PackedByteArray out;

	if (valid_data) {
		// Try decoding the last received packet.
		ret = decode(current_packet, out);
		if (ret == 0) {	
			ring_buffer.advance_read(1);
			return PackedByteArray();
		} else {
			valid_data = 0;
		}
	}

	if (!ring_buffer.read(&packet, sizeof(JitterBufferPacket))) {
		// No packet found
		decode(PackedByteArray(), out);
	} else {
		// Decode packet
		ret = decode(packet.data, out);
		if (ret == 0) {
			valid_data = 1;
			current_packet = packet.data;
			r_metadata["current_timestamp"] = packet.timestamp;
		} else {
			// Error while decoding
			memset(out.ptrw(), 0, frame_size);
		}
	}

	ring_buffer.advance_read(1);
	return out;
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
