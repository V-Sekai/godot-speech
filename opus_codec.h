/*************************************************************************/
/*  opus_codec.h                                                         */
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

#ifndef OPUS_CODEC_HPP
#define OPUS_CODEC_HPP

#include "speech_decoder.h"

#include "thirdparty/opus/opus/opus.h"
#include <stdint.h>

// TODO: always assumes little endian

class OpusCodec {
private:
  const uint32_t sample_rate = 48000;
  const uint32_t channel_count = 0;
  static const uint32_t APPLICATION = OPUS_APPLICATION_VOIP;
  static const uint32_t MILLISECONDS_PER_PACKET = 100;
  const int BUFFER_FRAME_COUNT = sample_rate / MILLISECONDS_PER_PACKET;

  static const int INTERNAL_BUFFER_SIZE = (25 * 3 * 1276);
  unsigned char internal_buffer[INTERNAL_BUFFER_SIZE];

  OpusEncoder *encoder = nullptr;

protected:
  void print_opus_error(int error_code) {
    switch (error_code) {
    case OPUS_OK:
      print_line("OpusCodec::OPUS_OK");
      break;
    case OPUS_BAD_ARG:
      print_line("OpusCodec::OPUS_BAD_ARG");
      break;
    case OPUS_BUFFER_TOO_SMALL:
      print_line("OpusCodec::OPUS_BUFFER_TOO_SMALL");
      break;
    case OPUS_INTERNAL_ERROR:
      print_line("OpusCodec::OPUS_INTERNAL_ERROR");
      break;
    case OPUS_INVALID_PACKET:
      print_line("OpusCodec::OPUS_INVALID_PACKET");
      break;
    case OPUS_UNIMPLEMENTED:
      print_line("OpusCodec::OPUS_UNIMPLEMENTED");
      break;
    case OPUS_INVALID_STATE:
      print_line("OpusCodec::OPUS_INVALID_STATE");
      break;
    case OPUS_ALLOC_FAIL:
      print_line("OpusCodec::OPUS_ALLOC_FAIL");
      break;
    }
  }

public:
  Ref<SpeechDecoder> get_speech_decoder() {
    int error;
    ::OpusDecoder *decoder =
        opus_decoder_create(sample_rate, channel_count, &error);
    if (error != OPUS_OK) {
      ERR_PRINT("OpusCodec: could not create Opus decoder!");
      return nullptr;
    }

    Ref<SpeechDecoder> speech_decoder;
    speech_decoder.instantiate();
    speech_decoder->set_decoder(decoder);

    return speech_decoder;
  }

  int encode_buffer(const PackedByteArray *p_pcm_buffer,
                    PackedByteArray *p_output_buffer) {
    int number_of_bytes = -1;

    if (get_speech_decoder().is_valid() && get_speech_decoder()->get_compression()) {
      // The following line disables compression and sends data uncompressed.
      // Combine it with a change in speech_decoder.h
      memcpy(p_output_buffer->ptrw(), p_pcm_buffer->ptr() + 1,
             BUFFER_FRAME_COUNT * 2 - 1);
      return BUFFER_FRAME_COUNT * 2 - 1;
    }
    if (encoder) {
      const opus_int16 *pcm_buffer_pointer =
          reinterpret_cast<const opus_int16 *>(p_pcm_buffer->ptr());

      opus_int32 ret_value =
          opus_encode(encoder, pcm_buffer_pointer, BUFFER_FRAME_COUNT,
                      internal_buffer, INTERNAL_BUFFER_SIZE);
      if (ret_value >= 0) {
        number_of_bytes = ret_value;

        if (number_of_bytes > 0) {
          unsigned char *output_buffer_pointer =
              reinterpret_cast<unsigned char *>(p_output_buffer->ptrw());
          memcpy(output_buffer_pointer, internal_buffer, number_of_bytes);
        }
      } else {
        print_opus_error(ret_value);
      }
    }

    return number_of_bytes;
  }

  bool decode_buffer(SpeechDecoder *p_speech_decoder,
                     const PackedByteArray *p_compressed_buffer,
                     PackedByteArray *p_pcm_output_buffer,
                     const int p_compressed_buffer_size,
                     const int p_pcm_output_buffer_size) {
    if (p_pcm_output_buffer->size() != p_pcm_output_buffer_size) {
      ERR_PRINT("OpusCodec: decode_buffer output_buffer_size mismatch!");
      return false;
    }

    return p_speech_decoder->process(
        p_compressed_buffer, p_pcm_output_buffer, p_compressed_buffer_size,
        p_pcm_output_buffer_size, BUFFER_FRAME_COUNT);
  }

  OpusCodec(uint32_t p_channel_count) : channel_count(p_channel_count) {
    int error = 0;
    encoder =
        opus_encoder_create(sample_rate, channel_count, APPLICATION, &error);

    if (error != OPUS_OK) {
      ERR_PRINT("OpusCodec: could not create Opus encoder!");
    }
    // allowed half-sample-rate.
    // error = opus_encoder_ctl(encoder,
    // OPUS_SET_BANDWIDTH(OPUS_BANDWIDTH_FULLBAND)); //OPUS_AUTO));
    if (error != OPUS_OK) {
      print_opus_error(error);
    }
    // error = opus_encoder_ctl(encoder, OPUS_SET_BITRATE(512000));
    if (error != OPUS_OK) {
      print_opus_error(error);
    }
  }

  ~OpusCodec() { opus_encoder_destroy(encoder); }
};

#endif // OPUS_CODEC_HPP