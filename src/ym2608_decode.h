#ifndef __H_YM2608_DECODE__
#define __H_YM2608_DECODE__

#include <stdio.h>
#include <stdint.h>
#include <stddef.h>

#define ADPCMLIB_CONV_TABLE_SIZE (141312)

typedef struct {

  int32_t sample_rate;
  int16_t channels;

  size_t resample_counter;

  size_t decode_buffer_len;
  size_t decode_buffer_ofs;
  int16_t* decode_buffer;

  uint8_t* conv_table;

} YM2608_DECODE_HANDLE;

int32_t ym2608_decode_init(YM2608_DECODE_HANDLE* nas, size_t decode_buffer_bytes, int32_t sample_rate, int16_t channels);
void ym2608_decode_close(YM2608_DECODE_HANDLE* nas);
size_t ym2608_decode_exec_buffer(YM2608_DECODE_HANDLE* nas, uint8_t* adpcm_data, size_t adpcm_data_bytes, int16_t* decode_buffer, size_t decode_buffer_len);
size_t ym2608_decode_exec(YM2608_DECODE_HANDLE* nas, uint8_t* adpcm_data, size_t adpcm_data_bytes);
size_t ym2608_decode_resample(YM2608_DECODE_HANDLE* nas, int16_t* resample_buffer, int32_t resample_freq, int16_t gain);

#endif