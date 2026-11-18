/*
 * wav.c - RIFF/WAVE header parser and writer. See wav.h.
 */
#include "wav.h"
#include <string.h>

static uint32_t P_Le32(const uint8_t *p)
{
  return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static uint16_t P_Le16(const uint8_t *p)
{
  return (uint16_t)(p[0] | (p[1] << 8));
}

static void P_Put32(uint8_t *p, uint32_t v)
{
  p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); p[2] = (uint8_t)(v >> 16); p[3] = (uint8_t)(v >> 24);
}

static void P_Put16(uint8_t *p, uint16_t v)
{
  p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8);
}

WavResult Wav_Parse(WavReadFn read, void *ctx, uint32_t file_size, WavInfo *out)
{
  uint8_t hdr[12];
  int have_fmt = 0;
  memset(out, 0, sizeof *out);

  if (read(ctx, 0, hdr, 12) != 12) return WAV_ERR_READ;
  if (memcmp(hdr, "RIFF", 4) != 0 || memcmp(hdr + 8, "WAVE", 4) != 0) return WAV_ERR_NOT_RIFF;

  uint32_t pos = 12;
  while (pos + 8u <= file_size)
  {
    uint8_t ch[8];
    if (read(ctx, pos, ch, 8) != 8) return WAV_ERR_READ;
    uint32_t size = P_Le32(ch + 4);
    uint32_t body = pos + 8u;

    if (memcmp(ch, "fmt ", 4) == 0)
    {
      uint8_t f[16];
      if (size < 16u || read(ctx, body, f, 16) != 16) return WAV_ERR_NO_FMT;
      out->format = P_Le16(f);
      out->channels = P_Le16(f + 2);
      out->sample_rate = P_Le32(f + 4);
      out->block_align = P_Le16(f + 12);
      out->bits = P_Le16(f + 14);
      have_fmt = 1;
    }
    else if (memcmp(ch, "data", 4) == 0)
    {
      if (!have_fmt) return WAV_ERR_NO_FMT;
      out->data_offset = body;
      uint32_t avail = (file_size > body) ? file_size - body : 0u;
      /* size 0 or 0xFFFFFFFF: streaming writers; use the rest of the file */
      out->data_size = (size == 0u || size > avail) ? avail : size;
      if (out->block_align) out->data_size -= out->data_size % out->block_align;

      int pcm = (out->format == 1u || out->format == 0xFFFEu);
      int bits_ok = (out->bits == 8u || out->bits == 16u);
      int ch_ok = (out->channels == 1u || out->channels == 2u);
      int rate_ok = (out->sample_rate >= 8000u && out->sample_rate <= 96000u);
      int align_ok = (out->block_align == out->channels * (out->bits / 8u));
      if (!pcm || !bits_ok || !ch_ok || !rate_ok || !align_ok) return WAV_ERR_UNSUPPORTED;
      return WAV_OK;
    }
    if (size > file_size - body) break;   /* broken chunk size */
    pos = body + size + (size & 1u);      /* chunks are word aligned */
  }
  return have_fmt ? WAV_ERR_NO_DATA : WAV_ERR_NO_FMT;
}

const char *Wav_ResultText(WavResult r)
{
  switch (r)
  {
    case WAV_OK:              return "ok";
    case WAV_ERR_READ:        return "read error";
    case WAV_ERR_NOT_RIFF:    return "not a WAV file";
    case WAV_ERR_NO_FMT:      return "no fmt chunk";
    case WAV_ERR_NO_DATA:     return "no data chunk";
    case WAV_ERR_UNSUPPORTED: return "unsupported format (PCM 8/16-bit, mono/stereo, 8-96 kHz)";
  }
  return "?";
}

void Wav_MakeHeader(uint8_t h[WAV_HEADER_SIZE], uint32_t rate, uint16_t channels,
                    uint16_t bits, uint32_t data_size)
{
  uint16_t align = (uint16_t)(channels * (bits / 8u));
  memcpy(h, "RIFF", 4);
  P_Put32(h + 4, 36u + data_size);
  memcpy(h + 8, "WAVEfmt ", 8);
  P_Put32(h + 16, 16u);
  P_Put16(h + 20, 1u);
  P_Put16(h + 22, channels);
  P_Put32(h + 24, rate);
  P_Put32(h + 28, rate * align);
  P_Put16(h + 32, align);
  P_Put16(h + 34, bits);
  memcpy(h + 36, "data", 4);
  P_Put32(h + 40, data_size);
}

size_t Wav_ToStereo16(uint8_t *buf, size_t in_bytes, uint16_t channels, uint16_t bits)
{
  if (bits == 16u && channels == 2u) return in_bytes;

  int16_t *out = (int16_t *)(void *)buf;
  if (bits == 16u)                  /* mono 16 -> stereo, backwards so it works in place */
  {
    size_t n = in_bytes / 2u;
    const int16_t *in = (const int16_t *)(const void *)buf;
    for (size_t i = n; i-- > 0;)
    {
      int16_t s = in[i];
      out[2u * i] = s;
      out[2u * i + 1u] = s;
    }
    return n * 4u;
  }
  /* 8-bit unsigned */
  size_t frames = in_bytes / channels;
  for (size_t i = frames; i-- > 0;)
  {
    int16_t l = (int16_t)(((int)buf[i * channels] - 128) << 8);
    int16_t r = (channels == 2u) ? (int16_t)(((int)buf[i * channels + 1u] - 128) << 8) : l;
    out[2u * i] = l;
    out[2u * i + 1u] = r;
  }
  return frames * 4u;
}
