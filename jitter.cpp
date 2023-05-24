/* Copyright (C) 2002 Jean-Marc Valin
   File: speex_jitter.h

   Adaptive jitter buffer for Speex

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

/*
TODO:
- Add short-term estimate
- Defensive programming
  + warn when last returned < last desired (begative buffering)
  + warn if update_delay not called between get() and tick() or is called twice in a row
- Linked list structure for holding the packets instead of the current fixed-size array
  + return memory to a pool
  + allow pre-allocation of the pool
  + optional max number of elements
- Statistics
  + drift
  + loss
  + late
  + jitter
  + buffering delay
*/

#include "jitter.h"

void VoipJitterBuffer::jitter_buffer_reset(Ref<JitterBuffer> jitter) {
	int i;
	for (i = 0; i < SPEEX_JITTER_MAX_BUFFER_SIZE; i++) {
		if (!jitter->packets[i]->get_data().is_empty()) {
			jitter->packets[i]->get_data().clear();
		}
	}
	/* Timestamp is actually undefined at this point */
	jitter->pointer_timestamp = 0;
	jitter->next_stop = 0;
	jitter->reset_state = 1;
	jitter->lost_count = 0;
	jitter->buffered = 0;
	jitter->auto_tradeoff = 32000;

	for (i = 0; i < MAX_BUFFERS; i++) {
		tb_init(jitter->_tb[i]);
		jitter->timeBuffers[i] = jitter->_tb[i];
	}
	/*fprintf (stderr, "reset\n");*/
}

int VoipJitterBuffer::jitter_buffer_ctl(Ref<JitterBuffer> jitter, int request, void *ptr) {
	int count, i;
	switch (request) {
		case JITTER_BUFFER_SET_MARGIN:
			jitter->buffer_margin = *(int32_t *)ptr;
			break;
		case JITTER_BUFFER_GET_MARGIN:
			*(int32_t *)ptr = jitter->buffer_margin;
			break;
		case JITTER_BUFFER_GET_AVALIABLE_COUNT:
			count = 0;
			for (i = 0; i < SPEEX_JITTER_MAX_BUFFER_SIZE; i++) {
				if (!jitter->packets[i]->get_data().is_empty() && LE32(jitter->pointer_timestamp, jitter->packets[i]->get_timestamp())) {
					count++;
				}
			}
			*(int32_t *)ptr = count;
			break;
		case JITTER_BUFFER_SET_DESTROY_CALLBACK:
			jitter->destroy = (void (*)(void *))ptr;
			break;
		case JITTER_BUFFER_GET_DESTROY_CALLBACK:
			*(void (**)(void *))ptr = jitter->destroy;
			break;
		case JITTER_BUFFER_SET_DELAY_STEP:
			jitter->delay_step = *(int32_t *)ptr;
			break;
		case JITTER_BUFFER_GET_DELAY_STEP:
			*(int32_t *)ptr = jitter->delay_step;
			break;
		case JITTER_BUFFER_SET_CONCEALMENT_SIZE:
			jitter->concealment_size = *(int32_t *)ptr;
			break;
		case JITTER_BUFFER_GET_CONCEALMENT_SIZE:
			*(int32_t *)ptr = jitter->concealment_size;
			break;
		case JITTER_BUFFER_SET_MAX_LATE_RATE:
			jitter->max_late_rate = *(int32_t *)ptr;
			jitter->window_size = 100 * TOP_DELAY / jitter->max_late_rate;
			jitter->subwindow_size = jitter->window_size / MAX_BUFFERS;
			break;
		case JITTER_BUFFER_GET_MAX_LATE_RATE:
			*(int32_t *)ptr = jitter->max_late_rate;
			break;
		case JITTER_BUFFER_SET_LATE_COST:
			jitter->latency_tradeoff = *(int32_t *)ptr;
			break;
		case JITTER_BUFFER_GET_LATE_COST:
			*(int32_t *)ptr = jitter->latency_tradeoff;
			break;
		default:
			ERR_PRINT(vformat("Unknown jitter_buffer_ctl request: %d", request));
			return -1;
	}
	return 0;
}

Ref<JitterBuffer> VoipJitterBuffer::jitter_buffer_init(int step_size) {
	Ref<JitterBuffer> jitter;
    jitter.instantiate();
    int i;
    int32_t tmp;
    for (i = 0; i < SPEEX_JITTER_MAX_BUFFER_SIZE; i++) {
        jitter->packets[i]->get_data().clear();
    }
    jitter->delay_step = step_size;
    jitter->concealment_size = step_size;
    /*FIXME: Should this be 0 or 1?*/
    jitter->buffer_margin = 0;
    jitter->late_cutoff = 50;
    jitter->destroy = nullptr;
    jitter->latency_tradeoff = 0;
    jitter->auto_adjust = 1;
    tmp = 4;
    jitter_buffer_ctl(jitter, JITTER_BUFFER_SET_MAX_LATE_RATE, &tmp);
    jitter_buffer_reset(jitter);
	return jitter;
}

void VoipJitterBuffer::jitter_buffer_destroy(Ref<JitterBuffer> jitter) {
	jitter_buffer_reset(jitter);
	jitter.unref();
}

void VoipJitterBuffer::jitter_buffer_put(Ref<JitterBuffer> jitter, const Ref<JitterBufferPacket> packet) {
	int i, j;
	int late;
	/*fprintf (stderr, "put packet %d %d\n", timestamp, span);*/

	/* Cleanup buffer (remove old packets that weren't played) */
	if (!jitter->reset_state) {
		for (i = 0; i < SPEEX_JITTER_MAX_BUFFER_SIZE; i++) {
			/* Make sure we don't discard a "just-late" packet in case we want to play it next (if we interpolate). */
			if (!jitter->packets[i]->get_data().is_empty() && LE32(jitter->packets[i]->get_timestamp() + jitter->packets[i]->get_span(), jitter->pointer_timestamp)) {
				/*fprintf (stderr, "cleaned (not played)\n");*/
				jitter->packets[i]->get_data().clear();
			}
		}
	}

	/*fprintf(stderr, "arrival: %d %d %d\n", packet->timestamp, jitter->next_stop, jitter->pointer_timestamp);*/
	/* Check if packet is late (could still be useful though) */
	if (!jitter->reset_state && LT32(packet->get_timestamp(), jitter->next_stop)) {
		update_timings(jitter, ((int32_t)packet->get_timestamp()) - ((int32_t)jitter->next_stop) - jitter->buffer_margin);
		late = 1;
	} else {
		late = 0;
	}

	/* For some reason, the consumer has failed the last 20 fetches. Make sure this packet is
	 * used to resync. */
	if (jitter->lost_count > 20) {
		jitter_buffer_reset(jitter);
	}

	/* Only insert the packet if it's not hopelessly late (i.e. totally useless) */
	if (jitter->reset_state || GE32(packet->get_timestamp() + packet->get_span() + jitter->delay_step, jitter->pointer_timestamp)) {
		/*Find an empty slot in the buffer*/
		for (i = 0; i < SPEEX_JITTER_MAX_BUFFER_SIZE; i++) {
			if (jitter->packets[i]->get_data().is_empty()) {
				break;
			}
		}

		/*No place left in the buffer, need to make room for it by discarding the oldest packet */
		if (i == SPEEX_JITTER_MAX_BUFFER_SIZE) {
			int earliest = jitter->packets[0]->get_timestamp();
			i = 0;
			for (j = 1; j < SPEEX_JITTER_MAX_BUFFER_SIZE; j++) {
				if (jitter->packets[i]->get_data().is_empty() || LT32(jitter->packets[j]->get_timestamp(), earliest)) {
					earliest = jitter->packets[j]->get_timestamp();
					i = j;
				}
			}
			jitter->packets[i]->get_data().clear();
			/*fprintf (stderr, "Buffer is full, discarding earliest frame %d (currently at %d)\n", timestamp, jitter->pointer_timestamp);*/
		}

		/* Copy packet in buffer */
		jitter->packets[i]->get_data() = packet->get_data();

		jitter->packets[i]->set_timestamp(packet->get_timestamp());
		jitter->packets[i]->set_span(packet->get_span());
		jitter->packets[i]->set_sequence(packet->get_sequence());
		jitter->packets[i]->set_user_data(packet->get_user_data());
		if (jitter->reset_state || late) {
			jitter->arrival[i] = 0;
		} else {
			jitter->arrival[i] = jitter->next_stop;
		}
	}
}

int VoipJitterBuffer::jitter_buffer_get(Ref<JitterBuffer> jitter, Ref<JitterBufferPacket> packet, int32_t desired_span, int32_t *start_offset) {
	int i;
	unsigned int j;
	int16_t opt;

	if (start_offset != nullptr) {
		*start_offset = 0;
	}

	/* Syncing on the first call */
	if (jitter->reset_state) {
		int found = 0;
		/* Find the oldest packet */
		uint32_t oldest = 0;
		for (i = 0; i < SPEEX_JITTER_MAX_BUFFER_SIZE; i++) {
			if (!jitter->packets[i]->get_data().is_empty() && (!found || LT32(jitter->packets[i]->get_timestamp(), oldest))) {
				oldest = jitter->packets[i]->get_timestamp();
				found = 1;
			}
		}
		if (found) {
			jitter->reset_state = 0;
			jitter->pointer_timestamp = oldest;
			jitter->next_stop = oldest;
		} else {
			packet->set_timestamp(0);
			packet->set_span(jitter->interp_requested);
			return JITTER_BUFFER_MISSING;
		}
	}

	jitter->last_returned_timestamp = jitter->pointer_timestamp;

	if (jitter->interp_requested != 0) {
		packet->set_timestamp(jitter->pointer_timestamp);
		packet->set_span(jitter->interp_requested);

		/* Increment the pointer because it got decremented in the delay update */
		jitter->pointer_timestamp += jitter->interp_requested;
		packet->get_data().clear();
		/*fprintf (stderr, "Deferred interpolate\n");*/

		jitter->interp_requested = 0;

		jitter->buffered = packet->get_span() - desired_span;

		return JITTER_BUFFER_INSERTION;
	}

	/* Searching for the packet that fits best */

	/* Search the buffer for a packet with the right timestamp and spanning the whole current chunk */
	for (i = 0; i < SPEEX_JITTER_MAX_BUFFER_SIZE; i++) {
		if (!jitter->packets[i]->get_data().is_empty() && jitter->packets[i]->get_timestamp() == jitter->pointer_timestamp && GE32(jitter->packets[i]->get_timestamp() + jitter->packets[i]->get_span(), jitter->pointer_timestamp + desired_span)) {
			break;
		}
	}

	/* If no match, try for an "older" packet that still spans (fully) the current chunk */
	if (i == SPEEX_JITTER_MAX_BUFFER_SIZE) {
		for (i = 0; i < SPEEX_JITTER_MAX_BUFFER_SIZE; i++) {
			if (!jitter->packets[i]->get_data().is_empty() && LE32(jitter->packets[i]->get_timestamp(), jitter->pointer_timestamp) && GE32(jitter->packets[i]->get_timestamp() + jitter->packets[i]->get_span(), jitter->pointer_timestamp + desired_span)) {
				break;
			}
		}
	}

	/* If still no match, try for an "older" packet that spans part of the current chunk */
	if (i == SPEEX_JITTER_MAX_BUFFER_SIZE) {
		for (i = 0; i < SPEEX_JITTER_MAX_BUFFER_SIZE; i++) {
			if (!jitter->packets[i]->get_data().is_empty() && LE32(jitter->packets[i]->get_timestamp(), jitter->pointer_timestamp) && GT32(jitter->packets[i]->get_timestamp() + jitter->packets[i]->get_span(), jitter->pointer_timestamp)) {
				break;
			}
		}
	}

	/* If still no match, try for earliest packet possible */
	if (i == SPEEX_JITTER_MAX_BUFFER_SIZE) {
		int found = 0;
		uint32_t best_time = 0;
		int best_span = 0;
		int besti = 0;
		for (i = 0; i < SPEEX_JITTER_MAX_BUFFER_SIZE; i++) {
			/* check if packet starts within current chunk */
			if (!jitter->packets[i]->get_data().is_empty() && LT32(jitter->packets[i]->get_timestamp(), jitter->pointer_timestamp + desired_span) && GE32(jitter->packets[i]->get_timestamp(), jitter->pointer_timestamp)) {
				if (!found || LT32(jitter->packets[i]->get_timestamp(), best_time) || (jitter->packets[i]->get_timestamp() == best_time && GT32(jitter->packets[i]->get_span(), best_span))) {
					best_time = jitter->packets[i]->get_timestamp();
					best_span = jitter->packets[i]->get_span();
					besti = i;
					found = 1;
				}
			}
		}
		if (found) {
			i = besti;
			/*fprintf (stderr, "incomplete: %d %d %d %d\n", jitter->packets[i]->timestamp, jitter->pointer_timestamp, chunk_size, jitter->packets[i]->span);*/
		}
	}

	/* If we find something */
	if (i != SPEEX_JITTER_MAX_BUFFER_SIZE) {
		int32_t offset;

		/* We (obviously) haven't lost this packet */
		jitter->lost_count = 0;

		/* In this case, 0 isn't as a valid timestamp */
		if (jitter->arrival[i] != 0) {
			update_timings(jitter, ((int32_t)jitter->packets[i]->get_timestamp()) - ((int32_t)jitter->arrival[i]) - jitter->buffer_margin);
		}

		/* Copy packet */
		packet->set_data(jitter->packets[i]->get_data());
		jitter->packets[i]->get_data().clear();

		/* Set timestamp and span (if requested) */
		offset = (int32_t)jitter->packets[i]->get_timestamp() - (int32_t)jitter->pointer_timestamp;
		if (start_offset != nullptr) {
			*start_offset = offset;
		} else if (offset != 0) {
			ERR_PRINT(vformat("jitter_buffer_get() discarding non-zero start_offset %d", offset));
		}

		packet->set_timestamp(jitter->packets[i]->get_timestamp());
		jitter->last_returned_timestamp = packet->get_timestamp();

		packet->set_span(jitter->packets[i]->get_span());
		packet->set_sequence(jitter->packets[i]->get_sequence());
		packet->set_user_data(jitter->packets[i]->get_user_data());
		/* Point to the end of the current packet */
		jitter->pointer_timestamp = jitter->packets[i]->get_timestamp() + jitter->packets[i]->get_span();

		jitter->buffered = packet->get_span() - desired_span;

		if (start_offset != nullptr) {
			jitter->buffered += *start_offset;
		}

		return JITTER_BUFFER_OK;
	}

	/* If we haven't found anything worth returning */

	/*fprintf (stderr, "not found\n");*/
	jitter->lost_count++;
	/*fprintf (stderr, "m");*/
	/*fprintf (stderr, "lost_count = %d\n", jitter->lost_count);*/

	opt = compute_opt_delay(jitter);

	/* Should we force an increase in the buffer or just do normal interpolation? */
	if (opt < 0) {
		/* Need to increase buffering */

		/* Shift histogram to compensate */
		shift_timings(jitter, -opt);

		packet->set_timestamp(jitter->pointer_timestamp);
		packet->set_span(-opt);
		/* Don't move the pointer_timestamp forward */
		packet->get_data().clear();

		jitter->buffered = packet->get_span() - desired_span;
		return JITTER_BUFFER_INSERTION;
		/*jitter->pointer_timestamp -= jitter->delay_step;*/
		/*fprintf (stderr, "Forced to interpolate\n");*/
	} else {
		/* Normal packet loss */
		packet->set_timestamp(jitter->pointer_timestamp);

		desired_span = ROUND_DOWN(desired_span, jitter->concealment_size);
		packet->set_span(desired_span);
		jitter->pointer_timestamp += desired_span;
		packet->get_data().clear();

		jitter->buffered = packet->get_span() - desired_span;
		return JITTER_BUFFER_MISSING;
		/*fprintf (stderr, "Normal loss\n");*/
	}
}

int VoipJitterBuffer::jitter_buffer_get_another(Ref<JitterBuffer> jitter, Ref<JitterBufferPacket> packet) {
	int i, j;
	for (i = 0; i < SPEEX_JITTER_MAX_BUFFER_SIZE; i++) {
		if (!jitter->packets[i]->get_data().is_empty() && jitter->packets[i]->get_timestamp() == jitter->last_returned_timestamp) {
			break;
		}
	}
	if (i != SPEEX_JITTER_MAX_BUFFER_SIZE) {
		/* Copy packet */
		packet->set_data(jitter->packets[i]->get_data());
		jitter->packets[i]->get_data().clear();
		packet->set_timestamp(jitter->packets[i]->get_timestamp());
		packet->set_span(jitter->packets[i]->get_span());
		packet->set_sequence(jitter->packets[i]->get_sequence());
		packet->set_user_data(jitter->packets[i]->get_user_data());
		return JITTER_BUFFER_OK;
	} else {
		packet->get_data().clear();
		packet->set_span(0);
		return JITTER_BUFFER_MISSING;
	}
}

int VoipJitterBuffer::jitter_buffer_update_delay(Ref<JitterBuffer> jitter, Ref<JitterBufferPacket> packet, int32_t *start_offset) {
	/* If the programmer calls jitter_buffer_update_delay() directly,
	   automatically disable auto-adjustment */
	jitter->auto_adjust = 0;

	return _jitter_buffer_update_delay(jitter, packet, start_offset);
}

int VoipJitterBuffer::jitter_buffer_get_pointer_timestamp(Ref<JitterBuffer> jitter) {
	return jitter->pointer_timestamp;
}

void VoipJitterBuffer::jitter_buffer_tick(Ref<JitterBuffer> jitter) {
	/* Automatically-adjust the buffering delay if requested */
	if (jitter->auto_adjust) {
		_jitter_buffer_update_delay(jitter, NULL, NULL);
	}

	if (jitter->buffered >= 0) {
		jitter->next_stop = jitter->pointer_timestamp - jitter->buffered;
	} else {
		jitter->next_stop = jitter->pointer_timestamp;
		ERR_PRINT(vformat("jitter buffer sees negative buffering, your code might be broken. Value is %d", jitter->buffered));
	}
	jitter->buffered = 0;
}

void VoipJitterBuffer::jitter_buffer_remaining_span(Ref<JitterBuffer> jitter, uint32_t rem) {
	/* Automatically-adjust the buffering delay if requested */
	if (jitter->auto_adjust) {
		_jitter_buffer_update_delay(jitter, NULL, NULL);
	}

	if (jitter->buffered < 0) {
		ERR_PRINT(vformat("jitter buffer sees negative buffering, your code might be broken. Value is %d", jitter->buffered));
	}
	jitter->next_stop = jitter->pointer_timestamp - rem;
}

void VoipJitterBuffer::tb_init(Ref<TimingBuffer> tb) {
	tb->set_filled(0);
	tb->set_curr_count(0);
}

void VoipJitterBuffer::tb_add(Ref<TimingBuffer> tb, int16_t timing) {
	int pos;
	/* Discard packet that won't make it into the list because they're too early */
	if (tb->get_filled() >= MAX_TIMINGS && timing >= tb->get_timing(tb->get_filled() - 1)) {
		tb->set_curr_count(tb->get_curr_count() + 1);
		return;
	}

	/* Find where the timing info goes in the sorted list using binary search */
	int32_t left = 0, right = tb->get_filled() - 1;
	while (left <= right) {
		int32_t mid = left + (right - left) / 2;
		if (tb->get_timing(mid) < timing) { // Checked the original code, it's correct.
			left = mid + 1;
		} else {
			right = mid - 1;
		}
	}
	pos = left;

	ERR_FAIL_COND(!(pos <= tb->get_filled() && pos < MAX_TIMINGS));

	/* Shift everything so we can perform the insertion */
	if (pos < tb->get_filled()) {
		int move_size = tb->get_filled() - pos;
		if (tb->get_filled() == MAX_TIMINGS) {
			move_size -= 1;
		}
		for (int i = 0; i < move_size; ++i) {
			tb->set_timing(pos + 1 + i, tb->get_timing(pos + i));
			tb->set_counts(pos + 1 + i, tb->get_counts(pos + i));
		}
	}
	/* Insert */
	tb->set_timing(pos, timing);
	tb->set_counts(pos, tb->get_curr_count());

	tb->set_curr_count(tb->get_curr_count() + 1);
	if (tb->get_filled() < MAX_TIMINGS) {
		tb->set_filled(tb->get_filled() + 1);
	}
}

int16_t VoipJitterBuffer::compute_opt_delay(Ref<JitterBuffer> jitter) {
	int i;
	int16_t opt = 0;
	int32_t best_cost = 0x7fffffff;
	int late = 0;
	int pos[MAX_BUFFERS];
	int tot_count;
	float late_factor;
	int penalty_taken = 0;
	int best = 0;
	int worst = 0;
	int32_t deltaT;

	/* Number of packet timings we have received (including those we didn't keep) */
	tot_count = 0;
	for (i = 0; i < MAX_BUFFERS; i++) {
		tot_count += jitter->_tb[i]->get_curr_count();
	}
	if (tot_count == 0) {
		return 0;
	}

	/* Compute cost for one lost packet */
	if (jitter->latency_tradeoff != 0) {
		late_factor = jitter->latency_tradeoff * 100.0f / tot_count;
	} else {
		late_factor = jitter->auto_tradeoff * jitter->window_size / tot_count;
	}

	/*fprintf(stderr, "late_factor = %f\n", late_factor);*/
	for (i = 0; i < MAX_BUFFERS; i++) {
		pos[i] = 0;
	}

	/* Pick the TOP_DELAY "latest" packets (doesn't need to actually be late
	   for the current settings) */
	for (i = 0; i < TOP_DELAY; i++) {
		int j;
		int next = -1;
		int latest = 32767;
		/* Pick latest among all sub-windows */
		for (j = 0; j < MAX_BUFFERS; j++) {
			if (pos[j] < jitter->_tb[j]->get_filled() && jitter->_tb[j]->get_timing(pos[j]) < latest) {
				next = j;
				latest = jitter->_tb[j]->get_timing(pos[j]);
			}
		}
		if (next != -1) {
			int32_t cost;

			if (i == 0) {
				worst = latest;
			}
			best = latest;
			latest = ROUND_DOWN(latest, jitter->delay_step);
			pos[next]++;

			/* Actual cost function that tells us how bad using this delay would be */
			cost = -latest + late_factor * late;
			/*fprintf(stderr, "cost %d = %d + %f * %d\n", cost, -latest, late_factor, late);*/
			if (cost < best_cost) {
				best_cost = cost;
				opt = latest;
			}
		} else {
			break;
		}

		/* For the next timing we will consider, there will be one more late packet to count */
		late++;
		/* Two-frame penalty if we're going to increase the amount of late frames (hysteresis) */
		if (latest >= 0 && !penalty_taken) {
			penalty_taken = 1;
			late += 4;
		}
	}

	deltaT = best - worst;
	/* This is a default "automatic latency tradeoff" when none is provided */
	jitter->auto_tradeoff = 1 + deltaT / TOP_DELAY;
	/*fprintf(stderr, "auto_tradeoff = %d (%d %d %d)\n", jitter->auto_tradeoff, best, worst, i);*/

	/* FIXME: Compute a short-term estimate too and combine with the long-term one */

	/* Prevents reducing the buffer size when we haven't really had much data */
	if (tot_count < TOP_DELAY && opt > 0) {
		return 0;
	}
	return opt;
}

void VoipJitterBuffer::update_timings(Ref<JitterBuffer> jitter, int32_t timing) {
	if (timing < -32767) {
		timing = -32767;
	}
	if (timing > 32767) {
		timing = 32767;
	}
	/* If the current sub-window is full, perform a rotation and discard oldest sub-widow */
	if (jitter->timeBuffers[0]->get_curr_count() >= jitter->subwindow_size) {
		int i;
		/*fprintf(stderr, "Rotate buffer\n");*/
		Ref<TimingBuffer> tmp = jitter->timeBuffers[MAX_BUFFERS - 1];
		for (i = MAX_BUFFERS - 1; i >= 1; i--) {
			jitter->timeBuffers[i] = jitter->timeBuffers[i - 1];
		}
		jitter->timeBuffers[0] = tmp;
		tb_init(jitter->timeBuffers[0]);
	}
	tb_add(jitter->timeBuffers[0], timing);
}

void VoipJitterBuffer::shift_timings(Ref<JitterBuffer> jitter, int16_t amount) {
	int i, j;
	for (i = 0; i < MAX_BUFFERS; i++) {
		for (j = 0; j < jitter->timeBuffers[i]->get_filled(); j++) {
			jitter->timeBuffers[i]->set_timing(j, jitter->timeBuffers[i]->get_timing(j) + amount);
		}
	}
}

int VoipJitterBuffer::_jitter_buffer_update_delay(Ref<JitterBuffer> jitter, Ref<JitterBufferPacket> packet, int32_t *start_offset) {
	int16_t opt = compute_opt_delay(jitter);
	/*fprintf(stderr, "opt adjustment is %d ", opt);*/

	if (opt < 0) {
		shift_timings(jitter, -opt);

		jitter->pointer_timestamp += opt;
		jitter->interp_requested = -opt;
		/*fprintf (stderr, "Decision to interpolate %d samples\n", -opt);*/
	} else if (opt > 0) {
		shift_timings(jitter, -opt);
		jitter->pointer_timestamp += opt;
		/*fprintf (stderr, "Decision to drop %d samples\n", opt);*/
	}

	return opt;
}
