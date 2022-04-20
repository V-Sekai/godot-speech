#include "speech_decoder.h"

bool SpeechDecoder::get_compression() const 
{ return is_compressed; }

void SpeechDecoder::set_compression(bool p_compressed) {
    is_compressed = p_compressed;
}

bool SpeechDecoder::process(const PackedByteArray *p_compressed_buffer,
                            PackedByteArray *p_pcm_output_buffer,
                            const int p_compressed_buffer_size,
                            const int p_pcm_output_buffer_size,
                            const int p_buffer_frame_count) {
  if (get_compression()) {
    // The following line disables compression and sends data uncompressed.
    // Combine it with a change in opus_codec.h
    if (p_compressed_buffer_size < p_pcm_output_buffer_size - 1) {
      return false;
    }
  }
  *p_pcm_output_buffer->ptrw() = 0;
  if (!decoder) {
    return false;
  }
  opus_int16 *output_buffer_pointer =
      reinterpret_cast<opus_int16 *>(p_pcm_output_buffer->ptrw());
  const unsigned char *opus_buffer_pointer =
      reinterpret_cast<const unsigned char *>(p_compressed_buffer->ptr());

  opus_int32 ret_value =
      opus_decode(decoder, opus_buffer_pointer, p_compressed_buffer_size,
                  output_buffer_pointer, p_buffer_frame_count, 0);
  return ret_value;
}
void SpeechDecoder::set_decoder(::OpusDecoder *p_decoder) {
  if (!decoder) {
    opus_decoder_destroy(decoder);
  }
  decoder = p_decoder;
}
void SpeechDecoder::_bind_methods() {
  ClassDB::bind_method(D_METHOD("set_compression", "compression"),
                       &SpeechDecoder::set_compression);
  ClassDB::bind_method(D_METHOD("get_compression"),
                       &SpeechDecoder::get_compression);
  ADD_PROPERTY(PropertyInfo(Variant::BOOL, "compression"), "set_compression",
               "get_compression");
}