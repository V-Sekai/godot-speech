#pragma once

class Jitter {
public:
	static void tb_init(struct TimingBuffer *tb) {
		tb->filled = 0;
		tb->curr_count = 0;
	}

	/* Add the timing of a new packet to the TimingBuffer */
	static void tb_add(struct TimingBuffer *tb, spx_int16_t timing);

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

	/** Buffer that keeps the time of arrival of the latest packets */
	struct TimingBuffer {
		int filled; /**< Number of entries occupied in "timing" and "counts"*/
		int curr_count; /**< Number of packet timings we got (including those we discarded) */
		spx_int32_t timing[MAX_TIMINGS]; /**< Sorted list of all timings ("latest" packets first) */
		spx_int16_t counts[MAX_TIMINGS]; /**< Order the packets were put in (will be used for short-term estimate) */
	};

	/** Jitter buffer structure */
	struct JitterBuffer_ {
		spx_uint32_t pointer_timestamp; /**< Timestamp of what we will *get* next */
		spx_uint32_t last_returned_timestamp; /**< Useful for getting the next packet with the same timestamp (for fragmented media) */
		spx_uint32_t next_stop; /**< Estimated time the next get() will be called */

		spx_int32_t buffered; /**< Amount of data we think is still buffered by the application (timestamp units)*/

		JitterBufferPacket packets[SPEEX_JITTER_MAX_BUFFER_SIZE]; /**< Packets stored in the buffer */
		spx_uint32_t arrival[SPEEX_JITTER_MAX_BUFFER_SIZE]; /**< Packet arrival time (0 means it was late, even though it's a valid timestamp) */

		void (*destroy)(void *); /**< Callback for destroying a packet */

		spx_int32_t delay_step; /**< Size of the steps when adjusting buffering (timestamp units) */
		spx_int32_t concealment_size; /**< Size of the packet loss concealment "units" */
		int reset_state; /**< True if state was just reset        */
		int buffer_margin; /**< How many frames we want to keep in the buffer (lower bound) */
		int late_cutoff; /**< How late must a packet be for it not to be considered at all */
		int interp_requested; /**< An interpolation is requested by speex_jitter_update_delay() */
		int auto_adjust; /**< Whether to automatically adjust the delay at any time */

		struct TimingBuffer _tb[MAX_BUFFERS]; /**< Don't use those directly */
		struct TimingBuffer *timeBuffers[MAX_BUFFERS]; /**< Storing arrival time of latest frames so we can compute some stats */
		int window_size; /**< Total window over which the late frames are counted */
		int subwindow_size; /**< Sub-window size for faster computation  */
		int max_late_rate; /**< Absolute maximum amount of late packets tolerable (in percent) */
		int latency_tradeoff; /**< Latency equivalent of losing one percent of packets */
		int auto_tradeoff; /**< Latency equivalent of losing one percent of packets (automatic default) */

		int lost_count; /**< Number of consecutive lost packets  */
	};

	/** Initialise jitter buffer */
	JitterBuffer *jitter_buffer_init(int step_size);

	/** Reset jitter buffer */
	void jitter_buffer_reset(JitterBuffer *jitter);

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

	/* Used like the ioctl function to control the jitter buffer parameters */
	int jitter_buffer_ctl(JitterBuffer *jitter, int request, void *ptr);
}