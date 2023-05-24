/* Copyright (C) 2002 Jean-Marc Valin */
/**
   @file speex_jitter.h
   @brief Adaptive jitter buffer for Speex
*/
/*
   Redistribution and use in source and binary forms, with or without
   modification, are permitted provided that the following conditions
   are met:

   - Redistributions of source code must retain the above copyright
   notice, this list of conditions and the following disclaimer.

   - Redistributions in binary form must reproduce the above copyright
   notice, this list of conditions and the following disclaimer in the
   documentation and/or other materials provided with the distribution.

   - Neither the name of the Xiph.org Foundation nor the names of its
   contributors may be used to endorse or promote products derived from
   this software without specific prior written permission.

   THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
   ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
   LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
   A PARTICULAR PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE FOUNDATION OR
   CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
   EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
   PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
   PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
   LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
   NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
   SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

*/

#ifndef SPEEX_JITTER_H
#define SPEEX_JITTER_H
/** @defgroup JitterBuffer JitterBuffer: Adaptive jitter buffer
 *  This is the jitter buffer that reorders UDP/RTP packets and adjusts the buffer size
 * to maintain good quality and low latency.
 *  @{
 */

#include <cstdint>
#include <cstdlib>
#include <cstring>

#include "core/object/ref_counted.h"
#include "core/variant/variant.h"

/** Packet has been retrieved */
#define JITTER_BUFFER_OK 0
/** Packet is lost or is late */
#define JITTER_BUFFER_MISSING 1
/** A "fake" packet is meant to be inserted here to increase buffering */
#define JITTER_BUFFER_INSERTION 2
/** There was an error in the jitter buffer */
#define JITTER_BUFFER_INTERNAL_ERROR -1
/** Invalid argument */
#define JITTER_BUFFER_BAD_ARGUMENT -2

/** Set minimum amount of extra buffering required (margin) */
#define JITTER_BUFFER_SET_MARGIN 0
/** Get minimum amount of extra buffering required (margin) */
#define JITTER_BUFFER_GET_MARGIN 1
/* JITTER_BUFFER_SET_AVAILABLE_COUNT wouldn't make sense */

/** Get the amount of available packets currently buffered */
#define JITTER_BUFFER_GET_AVAILABLE_COUNT 3
/** Included because of an early misspelling (will remove in next release) */
#define JITTER_BUFFER_GET_AVALIABLE_COUNT 3

/** Assign a function to destroy unused packet. When setting that, the jitter
	buffer no longer copies packet data. */
#define JITTER_BUFFER_SET_DESTROY_CALLBACK 4
/**  */
#define JITTER_BUFFER_GET_DESTROY_CALLBACK 5

/** Tell the jitter buffer to only adjust the delay in multiples of the step parameter provided */
#define JITTER_BUFFER_SET_DELAY_STEP 6
/**  */
#define JITTER_BUFFER_GET_DELAY_STEP 7

/** Tell the jitter buffer to only do concealment in multiples of the size parameter provided */
#define JITTER_BUFFER_SET_CONCEALMENT_SIZE 8
#define JITTER_BUFFER_GET_CONCEALMENT_SIZE 9

/** Absolute max amount of loss that can be tolerated regardless of the delay. Typical loss
	should be half of that or less. */
#define JITTER_BUFFER_SET_MAX_LATE_RATE 10
#define JITTER_BUFFER_GET_MAX_LATE_RATE 11

/** Equivalent cost of one percent late packet in timestamp units */
#define JITTER_BUFFER_SET_LATE_COST 12
#define JITTER_BUFFER_GET_LATE_COST 13

#define speex_assert(cond)                           \
	{                                                \
		if (!(cond)) {                               \
			speex_fatal("assertion failed: " #cond); \
		}                                            \
	}

#define SPEEX_JITTER_MAX_BUFFER_SIZE 200 /**< Maximum number of packets in jitter buffer */

#define TSUB(a, b) ((spx_int32_t)((a) - (b)))

#define GT32(a, b) (((spx_int32_t)((a) - (b))) > 0)
#define GE32(a, b) (((spx_int32_t)((a) - (b))) >= 0)
#define LT32(a, b) (((spx_int32_t)((a) - (b))) < 0)
#define LE32(a, b) (((spx_int32_t)((a) - (b))) <= 0)

#define ROUND_DOWN(x, step) ((x) < 0 ? ((x) - (step) + 1) / (step) * (step) : (x) / (step) * (step))

#define MAX_TIMINGS 40
#define MAX_BUFFERS 3
#define TOP_DELAY 40

#include "core/variant/variant.h"

typedef int16_t spx_int16_t;
typedef uint16_t spx_uint16_t;
typedef int32_t spx_int32_t;
typedef uint32_t spx_uint32_t;

/** Buffer that keeps the time of arrival of the latest packets */
class TimingBuffer : public RefCounted {
	GDCLASS(TimingBuffer, RefCounted);

	int filled = 0; /**< Number of entries occupied in "timing" and "counts"*/
	int curr_count = 0; /**< Number of packet timings we got (including those we discarded) */
	spx_int32_t timing[MAX_TIMINGS] = {}; /**< Sorted list of all timings ("latest" packets first) */
	spx_int16_t counts[MAX_TIMINGS] = {}; /**< Order the packets were put in (will be used for short-term estimate) */

protected:
	static void _bind_methods() {
		ClassDB::bind_method(D_METHOD("set_filled", "filled"), &TimingBuffer::set_filled);
		ClassDB::bind_method(D_METHOD("get_filled"), &TimingBuffer::get_filled);
		ADD_PROPERTY(PropertyInfo(Variant::INT, "filled"), "set_filled", "get_filled");

		ClassDB::bind_method(D_METHOD("set_curr_count", "curr_count"), &TimingBuffer::set_curr_count);
		ClassDB::bind_method(D_METHOD("get_curr_count"), &TimingBuffer::get_curr_count);
		ADD_PROPERTY(PropertyInfo(Variant::INT, "curr_count"), "set_curr_count", "get_curr_count");

		ClassDB::bind_method(D_METHOD("set_timing", "index", "value"), &TimingBuffer::set_timing);
		ClassDB::bind_method(D_METHOD("get_timing", "index"), &TimingBuffer::get_timing);

		ClassDB::bind_method(D_METHOD("set_counts", "index", "value"), &TimingBuffer::set_counts);
		ClassDB::bind_method(D_METHOD("get_counts", "index"), &TimingBuffer::get_counts);
	}

public:
	void set_filled(int p_filled) {
		filled = p_filled;
	}

	void set_curr_count(int p_curr_count) {
		curr_count = p_curr_count;
	}

	void set_timing(int index, spx_int32_t value) {
		ERR_FAIL_INDEX(index, MAX_TIMINGS);
		timing[index] = value;
	}

	void set_counts(int index, spx_int16_t value) {
		ERR_FAIL_INDEX(index, MAX_TIMINGS);
		counts[index] = value;
	}

	int get_filled() const {
		return filled;
	}

	int get_curr_count() const {
		return curr_count;
	}

	spx_int32_t get_timing(int index) const {
		ERR_FAIL_INDEX_V(index, MAX_TIMINGS, 0);
		return timing[index];
	}

	spx_int16_t get_counts(int index) const {
		ERR_FAIL_INDEX_V(index, MAX_TIMINGS, 0);
		return counts[index];
	}

	TimingBuffer() {
		for (int i = 0; i < MAX_TIMINGS; ++i) {
			timing[i] = 0;
			counts[i] = 0;
		}
	}
};

/** Definition of an incoming packet */
struct _JitterBufferPacket {
	PackedByteArray data; /**< Data bytes contained in the packet */
	spx_uint32_t timestamp = 0; /**< Timestamp for the packet */
	spx_uint32_t span = 0; /**< Time covered by the packet (same units as timestamp) */
	spx_uint16_t sequence = 0; /**< RTP Sequence number if available (0 otherwise) */
	spx_uint32_t user_data = 0; /**< Put whatever data you like here (it's ignored by the jitter buffer) */
};

/** Generic adaptive jitter buffer state */
typedef struct JitterBuffer_ JitterBuffer;

/** Definition of an incoming packet */
typedef struct _JitterBufferPacket JitterBufferPacket;

/** Jitter buffer structure */
struct JitterBuffer_ {
	spx_uint32_t pointer_timestamp = 0U; /**< Timestamp of what we will *get* next */
	spx_uint32_t last_returned_timestamp = 0U; /**< Useful for getting the next packet with the same timestamp (for fragmented media) */
	spx_uint32_t next_stop = 0U; /**< Estimated time the next get() will be called */

	spx_int32_t buffered = 0; /**< Amount of data we think is still buffered by the application (timestamp units)*/

	JitterBufferPacket packets[SPEEX_JITTER_MAX_BUFFER_SIZE]; /**< Packets stored in the buffer */
	spx_uint32_t arrival[SPEEX_JITTER_MAX_BUFFER_SIZE]; /**< Packet arrival time (0 means it was late, even though it's a valid timestamp) */

	void (*destroy)(void *) = nullptr; /**< Callback for destroying a packet */

	spx_int32_t delay_step = 0; /**< Size of the steps when adjusting buffering (timestamp units) */
	spx_int32_t concealment_size = 0; /**< Size of the packet loss concealment "units" */
	int reset_state = 0; /**< True if state was just reset        */
	int buffer_margin = 0; /**< How many frames we want to keep in the buffer (lower bound) */
	int late_cutoff = 0; /**< How late must a packet be for it not to be considered at all */
	int interp_requested = 0; /**< An interpolation is requested by speex_jitter_update_delay() */
	int auto_adjust = 0; /**< Whether to automatically adjust the delay at any time */

	Ref<TimingBuffer> _tb[MAX_BUFFERS] = {}; /**< Don't use those directly */
	Ref<TimingBuffer> timeBuffers[MAX_BUFFERS] = {}; /**< Storing arrival time of latest frames so we can compute some stats */
	int window_size = 0; /**< Total window over which the late frames are counted */
	int subwindow_size = 0; /**< Sub-window size for faster computation  */
	int max_late_rate = 0; /**< Absolute maximum amount of late packets tolerable (in percent) */
	int latency_tradeoff = 0; /**< Latency equivalent of losing one percent of packets */
	int auto_tradeoff = 0; /**< Latency equivalent of losing one percent of packets (automatic default) */

	int lost_count = 0; /**< Number of consecutive lost packets  */
	JitterBuffer_() {
		for (int i = 0; i < MAX_BUFFERS; ++i) {
			_tb[i].instantiate();
			timeBuffers[i] = _tb[i];
		}
	}
};

class VoipJitterBuffer : RefCounted {
	GDCLASS(VoipJitterBuffer, RefCounted);

	/** Generic adaptive jitter buffer state */
	struct JitterBuffer_;

	/** Reset jitter buffer */
	void jitter_buffer_reset(JitterBuffer *jitter);

	/* Used like the ioctl function to control the jitter buffer parameters */
	int jitter_buffer_ctl(JitterBuffer *jitter, int request, void *ptr);

	/** Initialise jitter buffer */
	JitterBuffer *jitter_buffer_init(int step_size);

	/** Destroy jitter buffer */
	void jitter_buffer_destroy(JitterBuffer *jitter);

	/** Put one packet into the jitter buffer */
	void jitter_buffer_put(JitterBuffer *jitter, const JitterBufferPacket *packet);

	/** Get one packet from the jitter buffer */
	int jitter_buffer_get(JitterBuffer *jitter, JitterBufferPacket *packet, spx_int32_t desired_span, spx_int32_t *start_offset);

	int jitter_buffer_get_another(JitterBuffer *jitter, JitterBufferPacket *packet);

	/* Let the jitter buffer know it's the right time to adjust the buffering delay to the network conditions */
	int jitter_buffer_update_delay(JitterBuffer *jitter, JitterBufferPacket *packet, spx_int32_t *start_offset);

	/** Get pointer timestamp of jitter buffer */
	int jitter_buffer_get_pointer_timestamp(JitterBuffer *jitter);

	void jitter_buffer_tick(JitterBuffer *jitter);

	void jitter_buffer_remaining_span(JitterBuffer *jitter, spx_uint32_t rem);

public:
	static void tb_init(Ref<TimingBuffer>);

	/* Add the timing of a new packet to the TimingBuffer */
	static void tb_add(Ref<TimingBuffer>, spx_int16_t timing);

	/** Based on available data, this computes the optimal delay for the jitter buffer.
	   The optimised function is in timestamp units and is:
	   cost = delay + late_factor*[number of frames that would be late if we used that delay]
	   @param tb Array of buffers
	   @param late_factor Equivalent cost of a late frame (in timestamp units)
	 */
	static spx_int16_t compute_opt_delay(JitterBuffer *jitter);

	/** Take the following timing into consideration for future calculations */
	static void update_timings(JitterBuffer *jitter, spx_int32_t timing);

	/** Compensate all timings when we do an adjustment of the buffering */
	static void shift_timings(JitterBuffer *jitter, spx_int16_t amount);

	/* Let the jitter buffer know it's the right time to adjust the buffering delay to the network conditions */
	static int _jitter_buffer_update_delay(JitterBuffer *jitter, JitterBufferPacket *packet, spx_int32_t *start_offset);
};

#endif