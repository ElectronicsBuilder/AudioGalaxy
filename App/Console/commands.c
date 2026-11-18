/*
 * commands.c - AudioGalaxy console commands (USART3, 115200).
 */
#include "commands.h"
#include "cli.h"
#include "log.h"
#include "version.h"
#include "player.h"
#include "playlist.h"
#include "link.h"
#include "app_hal.h"
#include "cmsis_os2.h"
#include "FreeRTOS.h"
#include "task.h"
#include <stdio.h>
#include <string.h>

static int P_Post(GxId id, long arg)
{
  if (Player_Post(id, arg) != 0) return Cli_Error("player busy");
  return 0;
}

static int C_Ver(int argc, char **argv)
{
  (void)argc; (void)argv;
  Cli_Printf("name=%s", APP_NAME);
  Cli_Printf("version=%s", APP_VERSION);
  Cli_Printf("board=%s", APP_BOARD);
  Cli_Printf("built=%s %s", __DATE__, __TIME__);
  return 0;
}

static int C_Status(int argc, char **argv)
{
  (void)argc; (void)argv;
  PlayerStatus s;
  Player_GetStatus(&s);
  Cli_Printf("state=%s", Player_StateName(s.state));
  Cli_Printf("tracks=%d", s.tracks);
  Cli_Printf("track=%d", s.track);
  if (s.track >= 0 && Playlist_Name(s.track)) Cli_Printf("name=%s", Playlist_Name(s.track));
  Cli_Printf("volume=%u", s.volume);
  Cli_Printf("repeat=%s", s.repeat ? "on" : "off");
  if (s.sample_rate)
  {
    Cli_Printf("format=%lu Hz %u ch %u bit", (unsigned long)s.sample_rate, s.channels, s.bits);
    Cli_Printf("position=%lu.%lu/%lu s", (unsigned long)(s.position_ms / 1000u),
               (unsigned long)(s.position_ms % 1000u / 100u), (unsigned long)(s.length_ms / 1000u));
  }
  Cli_Printf("underruns=%lu", (unsigned long)s.underruns);
  return 0;
}

static int C_Ls(int argc, char **argv)
{
  (void)argc; (void)argv;
  int n = Playlist_Count();
  if (n == 0) return Cli_Error("no tracks (USB stick missing or no .wav in its root folder)");
  for (int i = 0; i < n; i++) Cli_Printf("%3d %s", i, Playlist_Name(i));
  Cli_Printf("tracks=%d", n);
  return 0;
}

static int C_Play(int argc, char **argv)
{
  if (argc < 2) return P_Post(GX_START, 0);
  long i;
  if (Cli_ParseInt(argv[1], 0, GX_PLAYLIST_MAX - 1, &i) == 0) return P_Post(GX_PLAY, i);
  if (Player_PlayName(argv[1]) != 0) return Cli_Error("player busy");
  return 0;
}

static int C_Next(int argc, char **argv)
{
  PlayerStatus s;
  Player_GetStatus(&s);
  if (s.tracks == 0) return Cli_Error("no tracks");
  int step = (strcmp(argv[0], "prev") == 0) ? s.tracks - 1 : 1;
  (void)argc;
  return P_Post(GX_PLAY, (s.track + step) % s.tracks);
}

static int C_Simple(int argc, char **argv)
{
  (void)argc;
  if (strcmp(argv[0], "stop") == 0) return P_Post(GX_STOP, 0);
  if (strcmp(argv[0], "pause") == 0) return P_Post(GX_PAUSE, 0);
  if (strcmp(argv[0], "resume") == 0) return P_Post(GX_RESUME, 0);
  return Cli_Error("?");
}

static int C_Vol(int argc, char **argv)
{
  long v;
  if (argc < 2)
  {
    PlayerStatus s;
    Player_GetStatus(&s);
    Cli_Printf("volume=%u", s.volume);
    return 0;
  }
  if (Cli_ParseInt(argv[1], 0, GX_VOLUME_MAX, &v) != 0) return Cli_Error("volume is 0..100");
  return P_Post(GX_VOL, v);
}

static int C_Repeat(int argc, char **argv)
{
  if (argc < 2) return Cli_Error("usage: repeat on|off");
  if (strcmp(argv[1], "on") == 0) return P_Post(GX_REPEATON, 0);
  if (strcmp(argv[1], "off") == 0) return P_Post(GX_REPEATOFF, 0);
  return Cli_Error("usage: repeat on|off");
}

static int C_Rec(int argc, char **argv)
{
  if (argc < 2) return Cli_Error("usage: rec start|stop");
  if (strcmp(argv[1], "start") == 0) return P_Post(GX_RECORD, 0);
  if (strcmp(argv[1], "stop") == 0) return P_Post(GX_STOPRECORD, 0);
  return Cli_Error("usage: rec start|stop");
}

/* gx <GALAXY command...>: handle a link line as if BeaconGalaxy sent it */
static int C_Gx(int argc, char **argv)
{
  if (argc < 2) return Cli_Error("usage: gx GALAXYPLAY 3");
  char line[GX_LINE_MAX + 1];
  size_t pos = 0;
  for (int i = 1; i < argc && pos < GX_LINE_MAX; i++)
  {
    int quote = strchr(argv[i], ' ') != NULL;
    pos += (size_t)snprintf(line + pos, sizeof line - pos, quote ? "%s\"%s\"" : "%s%s",
                            i > 1 ? " " : "", argv[i]);
  }
  if (!Link_GetLog()) Cli_Printf("(replies go to the link; 'link log on' shows them here)");
  Link_HandleLine(line, 1);
  return 0;
}

static int C_Link(int argc, char **argv)
{
  if (argc >= 3 && strcmp(argv[1], "log") == 0)
  {
    Link_SetLog(strcmp(argv[2], "on") == 0);
    return 0;
  }
  LinkStats s;
  Link_GetStats(&s);
  Cli_Printf("log=%s", Link_GetLog() ? "on" : "off");
  Cli_Printf("rx_lines=%lu", s.rx_lines);
  Cli_Printf("tx_lines=%lu", s.tx_lines);
  Cli_Printf("rx_errors=%lu", s.rx_errors);
  Cli_Printf("rx_overruns=%lu", s.rx_overruns);
  return 0;
}

static int C_Tasks(int argc, char **argv)
{
  (void)argc; (void)argv;
  Cli_Printf("heap_free=%u", (unsigned)xPortGetFreeHeapSize());
  Cli_Printf("heap_min=%u", (unsigned)xPortGetMinimumEverFreeHeapSize());
  TaskStatus_t list[10];
  UBaseType_t n = uxTaskGetSystemState(list, 10, NULL);
  for (UBaseType_t i = 0; i < n; i++)
  {
    Cli_Printf("task=%-12s prio=%lu stack_free=%u", list[i].pcTaskName,
               (unsigned long)list[i].uxCurrentPriority, (unsigned)(list[i].usStackHighWaterMark * 4u));
  }
  return 0;
}

static int C_Log(int argc, char **argv)
{
  static const char *names[] = { "error", "warn", "info", "debug" };
  if (argc >= 2)
  {
    for (int i = 0; i < 4; i++)
    {
      if (strcmp(argv[1], names[i]) == 0) { log_level = (LogLevel)i; return 0; }
    }
    return Cli_Error("usage: log error|warn|info|debug");
  }
  Cli_Printf("log=%s", names[log_level]);
  return 0;
}

static int C_Reset(int argc, char **argv)
{
  (void)argc; (void)argv;
  Cli_Printf("restarting");
  osDelay(50);
  NVIC_SystemReset();
  return 0;
}

static const CliCommand commands[] = {
  { "ver",    "",                "firmware version", C_Ver, NULL },
  { "status", "",                "player state, track, volume, position", C_Status, NULL },
  { "ls",     "",                "list the tracks on the USB stick", C_Ls, NULL },
  { "play",   "[index|name]",    "play a track (no argument: start/resume)", C_Play,
    "play            start the last track (or resume)\n"
    "play 3          play track 3 (see ls)\n"
    "play fur-elise.wav" },
  { "next",   "",                "next track", C_Next, NULL },
  { "prev",   "",                "previous track", C_Next, NULL },
  { "stop",   "",                "stop playing / recording", C_Simple, NULL },
  { "pause",  "",                "pause", C_Simple, NULL },
  { "resume", "",                "resume", C_Simple, NULL },
  { "vol",    "[0..100]",        "show / set the volume", C_Vol, NULL },
  { "repeat", "on|off",          "repeat the track", C_Repeat, NULL },
  { "rec",    "start|stop",      "record rec.wav from the microphone (max 30 s)", C_Rec, NULL },
  { "gx",     "<GALAXY...>",     "handle a link command as if BeaconGalaxy sent it", C_Gx,
    "gx GALAXYGETPLAYLIST\n"
    "gx GALAXYPLAY 2\n"
    "gx GALAXYVOL 40\n"
    "Replies go to the link (USART2); 'link log on' shows them here." },
  { "link",   "[log on|off]",    "link statistics / log link lines", C_Link, NULL },
  { "tasks",  "",                "heap and task stacks", C_Tasks, NULL },
  { "log",    "[level]",         "show / set the log level", C_Log, NULL },
  { "reset",  "",                "restart the board", C_Reset, NULL },
};

void Commands_Register(void)
{
  Cli_Register(commands, sizeof commands / sizeof commands[0]);
}
