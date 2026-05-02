// fft.h: small fixed-size FFT for the music visualizer. Operates on
// 16-bit PCM samples; output buffer is FFT_BUFFER_SIZE bins.

#pragma once

#define FFT_BUFFER_SIZE_LOG 8
#define FFT_BUFFER_SIZE (1 << FFT_BUFFER_SIZE_LOG)
typedef short int sound_sample;
typedef struct _struct_fft_state fft_state;
fft_state* fft_init (void);
void fft_perform (const sound_sample* input, float* output, fft_state* state);
void fft_close (fft_state* state);
