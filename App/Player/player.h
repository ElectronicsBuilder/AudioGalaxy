/*
 * player.h - the player task: USB stick, playlist, WAV playback, recording.
 *
 * Every file access happens in this task. Other tasks post commands (the
 * GALAXY* link commands, see galaxy_link.h) with Player_Post(); results come
 * back as events through the callback given to Player_Start() (called from the
 * player task).
 */
#ifndef APP_PLAYER_H
#define APP_PLAYER_H

#include <stdint.h>
#include "galaxy_link.h"

typedef enum {
  PLAYER_NO_DISK = 0,
  PLAYER_IDLE,
  PLAYER_PLAYING,
  PLAYER_PAUSED,
  PLAYER_RECORDING,
} PlayerState;

typedef struct {
  PlayerState state;
  int      track;          /* current / last track, -1 = none */
  int      tracks;         /* playlist size */
  uint8_t  volume;         /* 0..100 */
  uint8_t  repeat;
  uint32_t sample_rate;
  uint16_t channels;
  uint16_t bits;
  uint32_t position_ms;
  uint32_t length_ms;
  uint32_t underruns;      /* refills that came too late */
} PlayerStatus;

/* event callback: id = GX_READY / GX_PLAYLIST / GX_EV_* (see galaxy_link.h) */
typedef void (*PlayerEventFn)(GxId id, long a, long b, const char *text);

#define PLAYER_REC_FILE   "rec.wav"
#define PLAYER_REC_MAX_S  30u      /* longest recording, seconds */

void Player_Start(PlayerEventFn on_event);
int  Player_Post(GxId cmd, long arg);         /* 0 = queued */
int  Player_PlayName(const char *name);       /* console helper: 0 = queued */
void Player_GetStatus(PlayerStatus *out);
const char *Player_StateName(PlayerState s);

/* from the BSP audio callbacks (interrupt context) */
void Player_IsrOutHalf(void);
void Player_IsrOutFull(void);
void Player_IsrOutError(void);
void Player_IsrInHalf(void);
void Player_IsrInFull(void);

#endif
