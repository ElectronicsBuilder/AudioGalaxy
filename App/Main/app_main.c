/*
 * app_main.c - AudioGalaxy application: start-up and the App task loop.
 *
 * Called from the CubeMX default task (Core/Src/freertos.c) after the USB
 * host is started. The App task runs every 5 ms: console input, link input,
 * user button, heartbeat LED. Audio and files run in the player task.
 */
#include "app_main.h"
#include "app_hal.h"
#include "version.h"
#include "cli.h"
#include "log.h"
#include "uart_io.h"
#include "swd_console.h"
#include "link.h"
#include "player.h"
#include "commands.h"
#include "cmsis_os2.h"

#define CONSOLE_PORT 0     /* USART3 PD8/PD9 */
#define SWD_PORT     1     /* RAM mailbox over the ST-LINK (tools/swd_console.py) */

static UartIo *console;
static osMutexId_t console_lock;

static void P_ConsoleWrite(const char *s, size_t len)
{
  if (console_lock) osMutexAcquire(console_lock, osWaitForever);
  UartIo_Write(console, s, len);
  if (console_lock) osMutexRelease(console_lock);
}

static void P_SwdWrite(const char *s, size_t len)
{
  if (console_lock) osMutexAcquire(console_lock, osWaitForever);
  SwdConsole_Write(s, len);
  if (console_lock) osMutexRelease(console_lock);
}

/* USART3 interrupt: called from stm32f4xx_it.c */
void APP_USART3_IRQ(void)
{
  UartIo_Irq(console);
}

/* user button: short press = pause/resume, long press (> 1 s) = next track */
static void P_Button(void)
{
  static uint32_t down_ms;
  static int was_down;
  int down = HAL_GPIO_ReadPin(USER_BUTTON_GPIO_Port, USER_BUTTON_Pin) == GPIO_PIN_SET;
  if (down)
  {
    down_ms += 5u;
    if (down_ms == 1000u)
    {
      PlayerStatus s;
      Player_GetStatus(&s);
      if (s.tracks > 0) Player_Post(GX_PLAY, (s.track + 1) % s.tracks);
    }
  }
  else if (was_down)
  {
    if (down_ms >= 30u && down_ms < 1000u)
    {
      PlayerStatus s;
      Player_GetStatus(&s);
      if (s.state == PLAYER_PLAYING) Player_Post(GX_PAUSE, 0);
      else if (s.state == PLAYER_PAUSED) Player_Post(GX_RESUME, 0);
      else Player_Post(GX_START, 0);
    }
    down_ms = 0;
  }
  was_down = down;
}

void App_Init(void)
{
  console_lock = osMutexNew(NULL);
  console = UartIo_Start(USART3);
  Cli_SetWriter(CONSOLE_PORT, P_ConsoleWrite);
  SwdConsole_Init();
  Cli_SetWriter(SWD_PORT, P_SwdWrite);
  Commands_Register();
  Cli_Init(APP_NAME " " APP_VERSION);
  LOG_I("%s %s on %s, %lu MHz", APP_NAME, APP_VERSION, APP_BOARD,
        (unsigned long)(HAL_RCC_GetSysClockFreq() / 1000000u));

  Link_Init();
  Player_Start(Link_Event);
  HAL_GPIO_WritePin(LED_GREEN_GPIO_Port, LED_GREEN_Pin, GPIO_PIN_SET);
  Link_Event(GX_READY, 0, 0, NULL);          /* tell BeaconGalaxy we restarted */
}

void App_Run(void)
{
  uint32_t next = osKernelGetTickCount();
  uint32_t beat = 0;
  for (;;)
  {
    next += 5u;
    uint32_t now = osKernelGetTickCount();
    if ((int32_t)(next - now) > 0) osDelayUntil(next);
    else next = now;                          /* fell behind: resync */

    uint8_t b;
    while (UartIo_Read(console, &b)) Cli_InputFrom(CONSOLE_PORT, (char)b);
    while (SwdConsole_Read(&b)) Cli_InputFrom(SWD_PORT, (char)b);
    Link_Poll();
    P_Button();

    if (++beat >= 100u)                       /* 0.5 s heartbeat */
    {
      beat = 0;
      HAL_GPIO_TogglePin(LED_ORANGE_GPIO_Port, LED_ORANGE_Pin);
    }
  }
}
