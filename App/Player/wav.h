/*
 * wav.h - RIFF/WAVE header parser and writer (no hardware access; tested on the PC).
 *
 * Wav_Parse walks the RIFF chunks (any order, odd sizes padded, LIST/fact
 * etc. skipped) until it has "fmt " and the start of "data". The caller
 * reads the file through a callback so the parser works on FatFs and in tests.
 */
#ifndef APP_WAV_H
#define APP_WAV_H

#include <stdint.h>
#include <stddef.h>

typedef struct {
  uint16_t format;          /* 1 = PCM, 0xFFFE = extensible */
  uint16_t channels;        /* 1 or 2 */
  uint32_t sample_rate;     /* Hz */
  uint16_t bits;            /* 8 or 16 */
  uint16_t block_align;     /* bytes per frame */
  uint32_t data_offset;     /* file offset of the first sample */
  uint32_t data_size;       /* bytes of samples (clipped to the file) */
} WavInfo;

typedef enum {
  WAV_OK = 0,
  WAV_ERR_READ,             /* the file ended / read failed */
  WAV_ERR_NOT_RIFF,
  WAV_ERR_NO_FMT,
  WAV_ERR_NO_DATA,
  WAV_ERR_UNSUPPORTED,      /* not PCM 8/16-bit mono/stereo 8-96 kHz */
} WavResult;

/* read len bytes at offset into buf; return bytes read */
typedef size_t (*WavReadFn)(void *ctx, uint32_t offset, void *buf, size_t len);

WavResult Wav_Parse(WavReadFn read, void *ctx, uint32_t file_size, WavInfo *out);
const char *Wav_ResultText(WavResult r);

/* 44-byte canonical PCM header for a recording */
#define WAV_HEADER_SIZE 44u
void Wav_MakeHeader(uint8_t hdr[WAV_HEADER_SIZE], uint32_t sample_rate,
                    uint16_t channels, uint16_t bits, uint32_t data_size);

/* Convert decoded samples to 16-bit stereo for the codec, in place.
 * buf holds in_bytes of source samples and must have room for the result
 * (4 bytes per frame). Returns the number of output bytes. */
size_t Wav_ToStereo16(uint8_t *buf, size_t in_bytes, uint16_t channels, uint16_t bits);

#endif
