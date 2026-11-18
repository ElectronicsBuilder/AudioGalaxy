/*
 * player.c - the player task. See player.h.
 *
 * Playback: a 16 KB buffer of 16-bit stereo samples is sent to the CS43L22
 * by I2S3 DMA in a loop. The half/full interrupts set a thread flag; the task
 * then refills the half that was just played (8 KB = 46 ms at 44.1 kHz).
 * Mono and 8-bit files are read smaller and widened in place.
 *
 * Recording: the MP45DT02 PDM microphone (I2S2) is filtered to 16 kHz mono
 * PCM in the DMA interrupts (1 ms per half), collected in a 2 x 4 KB buffer,
 * and the task writes each full half to rec.wav.
 */
#include "player.h"
#include "playlist.h"
#include "wav.h"
#include "log.h"
#include "app_hal.h"
#include "cmsis_os2.h"
#include "ff.h"
#include "fatfs.h"
#include "usb_host.h"
#include "stm32f4_discovery_audio.h"
#include <stdio.h>
#include <string.h>

extern ApplicationTypeDef Appli_state;

#define FLAG_OUT_HALF  0x01u
#define FLAG_OUT_FULL  0x02u
#define FLAG_OUT_ERR   0x04u
#define FLAG_IN_HALF   0x08u
#define FLAG_IN_FULL   0x10u
#define FLAG_WAKE      0x20u    /* a command was posted */
#define FLAG_ALL       0x3Fu

#define OUT_BUF_BYTES  16384u
#define OUT_HALF_BYTES (OUT_BUF_BYTES / 2u)
#define REC_HALF_SAMPLES 2048u

typedef struct {
  GxId cmd;
  long arg;
  char name[GX_NAME_MAX + 1];
} PlayerCmd;

static uint16_t out_buf[OUT_BUF_BYTES / 2u];
static uint16_t pdm_buf[INTERNAL_BUFF_SIZE];
static uint16_t rec_buf[2u * REC_HALF_SAMPLES];
static volatile uint32_t rec_fill;          /* samples written in rec_buf (ISR) */
static volatile uint32_t rec_overruns;

static osThreadId_t task;
static osMessageQueueId_t queue;
static PlayerEventFn emit;
static volatile PlayerStatus st;

static FIL file;
static int file_open;
static int mounted;
static WavInfo wav;
static uint32_t data_left;       /* bytes of samples not read yet */
static uint32_t data_done;       /* bytes of samples read */
static int end_halves;           /* halves of silence played after the end */
static uint32_t rec_bytes;
static uint32_t rec_start_tick;

/* ---------- small helpers ---------- */

const char *Player_StateName(PlayerState s)
{
  switch (s)
  {
    case PLAYER_NO_DISK:   return "nodisk";
    case PLAYER_IDLE:      return "idle";
    case PLAYER_PLAYING:   return "playing";
    case PLAYER_PAUSED:    return "paused";
    case PLAYER_RECORDING: return "recording";
  }
  return "?";
}

static void P_Event(GxId id, long a, long b, const char *text)
{
  if (emit != NULL) emit(id, a, b, text);
}

static void P_Led(GPIO_TypeDef *port, uint16_t pin, int on)
{
  HAL_GPIO_WritePin(port, pin, on ? GPIO_PIN_SET : GPIO_PIN_RESET);
}

static void P_SetState(PlayerState s)
{
  st.state = s;
  P_Led(LED_BLUE_GPIO_Port, LED_BLUE_Pin, s == PLAYER_PLAYING);
  P_Led(LED_RED_GPIO_Port, LED_RED_Pin, s == PLAYER_RECORDING);
}

static size_t P_FileRead(void *ctx, uint32_t offset, void *buf, size_t len)
{
  FIL *f = (FIL *)ctx;
  UINT got = 0;
  if (f_lseek(f, offset) != FR_OK) return 0;
  if (f_read(f, buf, (UINT)len, &got) != FR_OK) return 0;
  return got;
}

static void P_CloseFile(void)
{
  if (file_open)
  {
    f_close(&file);
    file_open = 0;
  }
}

/* ---------- playback ---------- */

/* Fill one half of out_buf with the next samples (or silence at the end). */
static void P_FillHalf(uint8_t *half)
{
  uint32_t frame_in = wav.block_align;                    /* bytes per source frame */
  uint32_t want = (OUT_HALF_BYTES / 4u) * frame_in;       /* source bytes for one half */
  if (want > data_left) want = data_left;

  UINT got = 0;
  if (want > 0 && f_read(&file, half, want, &got) != FR_OK) got = 0;
  data_left -= (got <= data_left) ? got : data_left;
  data_done += got;
  if (got < want) data_left = 0;                          /* read error: end the track */

  size_t out = Wav_ToStereo16(half, got, wav.channels, wav.bits);
  if (out < OUT_HALF_BYTES) memset(half + out, 0, OUT_HALF_BYTES - out);
  if (got == 0) end_halves++;

  if (wav.sample_rate != 0u)
  {
    st.position_ms = (uint32_t)((uint64_t)data_done * 1000u / ((uint64_t)wav.sample_rate * frame_in));
  }
}

static void P_StopAudio(void)
{
  if (st.state == PLAYER_PLAYING || st.state == PLAYER_PAUSED)
  {
    BSP_AUDIO_OUT_Stop(CODEC_PDWN_SW);
  }
  P_CloseFile();
}

static int P_PlayTrack(int index)
{
  const char *name = Playlist_Name(index);
  if (name == NULL)
  {
    P_Event(GX_EV_ERR, 0, 0, "bad track index");
    return -1;
  }
  P_StopAudio();
  osThreadFlagsClear(FLAG_OUT_HALF | FLAG_OUT_FULL | FLAG_OUT_ERR);

  char path[GX_NAME_MAX + 4];
  snprintf(path, sizeof path, "%s%s", USBHPath, name);
  FILINFO fno;
  if (f_stat(path, &fno) != FR_OK || f_open(&file, path, FA_READ) != FR_OK)
  {
    LOG_W("cannot open %s", name);
    P_Event(GX_EV_ERR, 0, 0, "cannot open file");
    P_SetState(PLAYER_IDLE);
    return -1;
  }
  file_open = 1;

  WavResult r = Wav_Parse(P_FileRead, &file, (uint32_t)fno.fsize, &wav);
  if (r != WAV_OK || f_lseek(&file, wav.data_offset) != FR_OK)
  {
    LOG_W("%s: %s", name, Wav_ResultText(r));
    P_Event(GX_EV_ERR, 0, 0, Wav_ResultText(r));
    P_CloseFile();
    P_SetState(PLAYER_IDLE);
    return -1;
  }

  st.track = index;
  st.sample_rate = wav.sample_rate;
  st.channels = wav.channels;
  st.bits = wav.bits;
  st.position_ms = 0;
  st.length_ms = (uint32_t)((uint64_t)wav.data_size * 1000u / ((uint64_t)wav.sample_rate * wav.block_align));
  data_left = wav.data_size;
  data_done = 0;
  end_halves = 0;

  if (BSP_AUDIO_OUT_Init(OUTPUT_DEVICE_AUTO, st.volume, wav.sample_rate) != AUDIO_OK)
  {
    LOG_E("audio codec init failed");
    P_Event(GX_EV_ERR, 0, 0, "codec init failed");
    P_CloseFile();
    P_SetState(PLAYER_IDLE);
    return -1;
  }
  P_FillHalf((uint8_t *)out_buf);
  P_FillHalf((uint8_t *)out_buf + OUT_HALF_BYTES);
  BSP_AUDIO_OUT_Play(out_buf, OUT_BUF_BYTES);
  P_SetState(PLAYER_PLAYING);
  LOG_I("play %d: %s (%lu Hz, %u ch, %u bit, %lu s)", index, name,
        (unsigned long)wav.sample_rate, wav.channels, wav.bits,
        (unsigned long)(st.length_ms / 1000u));
  P_Event(GX_EV_PLAYING, index, 0, NULL);
  return 0;
}

/* a half of the buffer was played: refill it, or finish the track */
static void P_OnHalfDone(uint8_t *half)
{
  if (st.state != PLAYER_PLAYING) return;
  if (data_left == 0 && end_halves >= 2)    /* both halves silent: the track is over */
  {
    P_StopAudio();
    P_SetState(PLAYER_IDLE);
    LOG_I("track %d ended", st.track);
    P_Event(GX_EV_WAVEEND, 0, 0, NULL);
    if (st.repeat) P_PlayTrack(st.track);
    return;
  }
  P_FillHalf(half);
}

/* ---------- recording ---------- */

static void P_StartRecord(void)
{
  if (!mounted)
  {
    P_Event(GX_EV_NOUSB, 0, 0, NULL);
    return;
  }
  P_StopAudio();
  char path[16];
  snprintf(path, sizeof path, "%s%s", USBHPath, PLAYER_REC_FILE);
  if (f_open(&file, path, FA_CREATE_ALWAYS | FA_WRITE) != FR_OK)
  {
    LOG_W("cannot create %s", PLAYER_REC_FILE);
    P_Event(GX_EV_ERR, 0, 0, "cannot create rec.wav");
    P_SetState(PLAYER_IDLE);
    return;
  }
  file_open = 1;
  uint8_t hdr[WAV_HEADER_SIZE];
  UINT wr;
  Wav_MakeHeader(hdr, DEFAULT_AUDIO_IN_FREQ, DEFAULT_AUDIO_IN_CHANNEL_NBR, 16, 0);
  f_write(&file, hdr, sizeof hdr, &wr);
  rec_bytes = 0;
  rec_fill = 0;
  rec_overruns = 0;
  osThreadFlagsClear(FLAG_IN_HALF | FLAG_IN_FULL);

  if (BSP_AUDIO_IN_Init(DEFAULT_AUDIO_IN_FREQ, DEFAULT_AUDIO_IN_BIT_RESOLUTION,
                        DEFAULT_AUDIO_IN_CHANNEL_NBR) != AUDIO_OK ||
      BSP_AUDIO_IN_Record(pdm_buf, INTERNAL_BUFF_SIZE) != AUDIO_OK)
  {
    LOG_E("microphone init failed");
    P_Event(GX_EV_ERR, 0, 0, "microphone init failed");
    P_CloseFile();
    P_SetState(PLAYER_IDLE);
    return;
  }
  rec_start_tick = osKernelGetTickCount();
  P_SetState(PLAYER_RECORDING);
  LOG_I("recording %s (max %u s)", PLAYER_REC_FILE, PLAYER_REC_MAX_S);
}

static void P_WriteRecHalf(const uint16_t *half)
{
  UINT wr = 0;
  if (f_write(&file, half, REC_HALF_SAMPLES * 2u, &wr) == FR_OK) rec_bytes += wr;
}

static void P_StopRecord(void)
{
  if (st.state != PLAYER_RECORDING) return;
  BSP_AUDIO_IN_Stop();
  /* write the part of the current half that is filled */
  uint32_t fill = rec_fill;
  uint32_t part = fill % REC_HALF_SAMPLES;
  const uint16_t *half = (fill >= REC_HALF_SAMPLES) ? &rec_buf[REC_HALF_SAMPLES] : &rec_buf[0];
  UINT wr = 0;
  if (part > 0 && f_write(&file, half, part * 2u, &wr) == FR_OK) rec_bytes += wr;

  uint8_t hdr[WAV_HEADER_SIZE];
  Wav_MakeHeader(hdr, DEFAULT_AUDIO_IN_FREQ, DEFAULT_AUDIO_IN_CHANNEL_NBR, 16, rec_bytes);
  f_lseek(&file, 0);
  f_write(&file, hdr, sizeof hdr, &wr);
  P_CloseFile();
  P_SetState(PLAYER_IDLE);
  LOG_I("recorded %lu ms (%lu overruns)", (unsigned long)(rec_bytes / 32u), (unsigned long)rec_overruns);

  /* rec.wav may be new: refresh the playlist */
  int n = Playlist_Scan(USBHPath);
  st.tracks = n;
  P_Event(GX_READY, n, 0, NULL);
}

/* ---------- USB stick ---------- */

static void P_PollDisk(void)
{
  ApplicationTypeDef usb = Appli_state;
  if (!mounted && usb == APPLICATION_READY)
  {
    if (f_mount(&USBHFatFS, USBHPath, 1) != FR_OK)
    {
      LOG_W("USB stick: mount failed");
      Appli_state = APPLICATION_IDLE;          /* wait for the next connect */
      return;
    }
    mounted = 1;
    int n = Playlist_Scan(USBHPath);
    st.tracks = n;
    P_SetState(PLAYER_IDLE);
    LOG_I("USB stick mounted: %d tracks", n);
    P_Event(GX_READY, n, 0, NULL);
    if (n == 0) P_Event(GX_EV_NOUSB, 0, 0, NULL);
  }
  else if (mounted && usb == APPLICATION_DISCONNECT)
  {
    if (st.state == PLAYER_RECORDING) BSP_AUDIO_IN_Stop();
    P_StopAudio();
    file_open = 0;                              /* the file object died with the stick */
    f_mount(NULL, USBHPath, 0);
    mounted = 0;
    Playlist_Clear();
    st.tracks = 0;
    P_SetState(PLAYER_NO_DISK);
    LOG_I("USB stick removed");
    P_Event(GX_EV_NOUSB, 0, 0, NULL);
  }
}

/* ---------- commands ---------- */

static void P_SendPlaylist(void)
{
  int n = Playlist_Count();
  if (n == 0)
  {
    P_Event(GX_EV_NOUSB, 0, 0, NULL);
    return;
  }
  for (int i = 0; i < n; i++) P_Event(GX_PLAYLIST, i, n, Playlist_Name(i));
}

static void P_SetVolume(long v)
{
  st.volume = (uint8_t)v;
  if (st.state == PLAYER_PLAYING || st.state == PLAYER_PAUSED) BSP_AUDIO_OUT_SetVolume(st.volume);
}

static void P_Command(const PlayerCmd *c)
{
  int busy_rec = (st.state == PLAYER_RECORDING);
  if (!mounted && c->cmd != GX_VOL && c->cmd != GX_RECALLVOL && c->cmd != GX_REPEATON &&
      c->cmd != GX_REPEATOFF && c->cmd != GX_HELLO)
  {
    P_Event(GX_EV_NOUSB, 0, 0, NULL);
    return;
  }
  switch (c->cmd)
  {
    case GX_HELLO:
      P_Event(GX_READY, st.tracks, 0, NULL);
      break;
    case GX_GETPLAYLIST:
      P_SendPlaylist();
      break;
    case GX_PLAY:
    case GX_PLAYTHEME:
      if (busy_rec) P_StopRecord();
      if (c->name[0] != '\0')
      {
        int i = Playlist_Find(c->name);
        if (i < 0) { P_Event(GX_EV_ERR, 0, 0, "no such track"); break; }
        P_PlayTrack(i);
      }
      else P_PlayTrack((int)c->arg);
      break;
    case GX_START:
      if (st.state == PLAYER_PAUSED) { BSP_AUDIO_OUT_Resume(); P_SetState(PLAYER_PLAYING); }
      else if (st.state == PLAYER_IDLE) P_PlayTrack(st.track >= 0 ? st.track : 0);
      break;
    case GX_STOP:
      if (busy_rec) P_StopRecord();
      else if (st.state == PLAYER_PLAYING || st.state == PLAYER_PAUSED)
      {
        P_StopAudio();
        P_SetState(PLAYER_IDLE);
        P_Event(GX_EV_STOPPED, 0, 0, NULL);
      }
      break;
    case GX_PAUSE:
      if (st.state == PLAYER_PLAYING) { BSP_AUDIO_OUT_Pause(); P_SetState(PLAYER_PAUSED); }
      break;
    case GX_RESUME:
      if (st.state == PLAYER_PAUSED) { BSP_AUDIO_OUT_Resume(); P_SetState(PLAYER_PLAYING); }
      break;
    case GX_VOL:
      P_SetVolume(c->arg);
      break;
    case GX_RECALLVOL:
      P_SetVolume(c->arg);
      P_Event(GX_EV_RECALLVOL, 0, 0, NULL);
      break;
    case GX_REPEATON:
    case GX_REPEATOFF:
      st.repeat = (c->cmd == GX_REPEATON);
      P_Event(GX_EV_REPEATSET, 0, 0, NULL);
      break;
    case GX_RECORD:
      if (!busy_rec) P_StartRecord();
      break;
    case GX_STOPRECORD:
      P_StopRecord();
      break;
    default:
      break;
  }
}

/* ---------- task ---------- */

static void P_Task(void *arg)
{
  (void)arg;
  P_SetState(PLAYER_NO_DISK);
  for (;;)
  {
    uint32_t flags = osThreadFlagsWait(FLAG_ALL, osFlagsWaitAny, 20);
    if (flags & osFlagsError) flags = 0;      /* timeout */

    if (flags & FLAG_OUT_ERR) st.underruns++;
    if (flags & FLAG_OUT_HALF) P_OnHalfDone((uint8_t *)out_buf);
    if (flags & FLAG_OUT_FULL) P_OnHalfDone((uint8_t *)out_buf + OUT_HALF_BYTES);

    if (st.state == PLAYER_RECORDING)
    {
      if (flags & FLAG_IN_HALF) P_WriteRecHalf(&rec_buf[0]);
      if (flags & FLAG_IN_FULL) P_WriteRecHalf(&rec_buf[REC_HALF_SAMPLES]);
      if (osKernelGetTickCount() - rec_start_tick >= PLAYER_REC_MAX_S * 1000u) P_StopRecord();
    }

    PlayerCmd c;
    while (osMessageQueueGet(queue, &c, NULL, 0) == osOK) P_Command(&c);
    P_PollDisk();
  }
}

void Player_Start(PlayerEventFn on_event)
{
  emit = on_event;
  st.track = -1;
  st.volume = 70;
  queue = osMessageQueueNew(16, sizeof(PlayerCmd), NULL);
  static const osThreadAttr_t attr = {
    .name = "player",
    .stack_size = 4096,
    .priority = osPriorityAboveNormal,
  };
  task = osThreadNew(P_Task, NULL, &attr);
}

static int P_Post(const PlayerCmd *c)
{
  if (queue == NULL || osMessageQueuePut(queue, c, 0, 10) != osOK) return -1;
  osThreadFlagsSet(task, FLAG_WAKE);
  return 0;
}

int Player_Post(GxId cmd, long arg)
{
  PlayerCmd c;
  memset(&c, 0, sizeof c);
  c.cmd = cmd;
  c.arg = arg;
  return P_Post(&c);
}

int Player_PlayName(const char *name)
{
  PlayerCmd c;
  memset(&c, 0, sizeof c);
  c.cmd = GX_PLAY;
  strncpy(c.name, name, GX_NAME_MAX);
  return P_Post(&c);
}

void Player_GetStatus(PlayerStatus *out)
{
  memcpy(out, (const void *)&st, sizeof *out);
}

/* ---------- interrupt side ---------- */

void Player_IsrOutHalf(void)  { osThreadFlagsSet(task, FLAG_OUT_HALF); }
void Player_IsrOutFull(void)  { osThreadFlagsSet(task, FLAG_OUT_FULL); }
void Player_IsrOutError(void) { osThreadFlagsSet(task, FLAG_OUT_ERR); }

static void P_InChunk(uint16_t *pdm)
{
  /* The BSP filter returns 1 ms as STEREO (the one microphone duplicated):
   * 2 * PCM_OUT_SIZE samples. Keep the left channel: rec.wav is mono. */
  static uint16_t pcm[2u * PCM_OUT_SIZE];
  uint32_t fill = rec_fill;
  BSP_AUDIO_IN_PDMToPCM(pdm, pcm);
  for (uint32_t i = 0; i < PCM_OUT_SIZE; i++) rec_buf[fill + i] = pcm[2u * i];
  fill += PCM_OUT_SIZE;
  if (fill == REC_HALF_SAMPLES) osThreadFlagsSet(task, FLAG_IN_HALF);
  if (fill >= 2u * REC_HALF_SAMPLES)
  {
    fill = 0;
    osThreadFlagsSet(task, FLAG_IN_FULL);
  }
  rec_fill = fill;
}

void Player_IsrInHalf(void) { P_InChunk(&pdm_buf[0]); }
void Player_IsrInFull(void) { P_InChunk(&pdm_buf[INTERNAL_BUFF_SIZE / 2u]); }
