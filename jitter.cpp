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
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "jitter.h"

#include "arch.h"
#include "speex/speex_jitter.h"
#include "os_support.h"

#ifndef NULL
#define NULL 0
#endif

#define SPEEX_JITTER_MAX_BUFFER_SIZE 200   /**< Maximum number of packets in jitter buffer */

#define TSUB(a,b) ((spx_int32_t)((a)-(b)))

#define GT32(a,b) (((spx_int32_t)((a)-(b)))>0)
#define GE32(a,b) (((spx_int32_t)((a)-(b)))>=0)
#define LT32(a,b) (((spx_int32_t)((a)-(b)))<0)
#define LE32(a,b) (((spx_int32_t)((a)-(b)))<=0)

#define ROUND_DOWN(x, step) ((x)<0 ? ((x)-(step)+1)/(step)*(step) : (x)/(step)*(step))

#define MAX_TIMINGS 40
#define MAX_BUFFERS 3
#define TOP_DELAY 40
