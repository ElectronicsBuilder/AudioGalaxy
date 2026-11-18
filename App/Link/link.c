/*
 * link.c - the serial link to BeaconGalaxy. See link.h.
 */
#include "link.h"
#include "player.h"
#include "uart_io.h"
#include "log.h"
#include "cmsis_os2.h"
#include <string.h>

static UartIo *uart;
static GxLineAsm rx_line;
static osMutexId_t tx_lock;
static int log_lines;
static LinkStats stats;

void Link_Init(void)
{
  uart = UartIo_Start(USART2);
  tx_lock = osMutexNew(NULL);
}

void Link_SetLog(int on) { log_lines = on; }
int  Link_GetLog(void)   { return log_lines; }

void Link_GetStats(LinkStats *out)
{
  stats.rx_overruns = UartIo_RxOverruns(uart);
  *out = stats;
}

static void P_Send(const char *line, size_t len)
{
  if (tx_lock) osMutexAcquire(tx_lock, osWaitForever);
  UartIo_Write(uart, line, len);
  stats.tx_lines++;
  if (tx_lock) osMutexRelease(tx_lock);
  if (log_lines)
  {
    char shown[GX_LINE_MAX + 8];
    size_t n = len;
    while (n > 0 && (line[n - 1] == '\r' || line[n - 1] == '\n')) n--;
    if (n > GX_LINE_MAX) n = GX_LINE_MAX;
    memcpy(shown, line, n);
    shown[n] = '\0';
    LOG_I("link> %s", shown);
  }
}

void Link_Event(GxId id, long a, long b, const char *text)
{
  char line[GX_LINE_MAX + 4];
  size_t n = Gx_FormatId(id, a, b, text, line, sizeof line);
  if (n > 0) P_Send(line, n);
}

void Link_HandleLine(char *line, int from_console)
{
  if (log_lines && !from_console) LOG_I("link< %s", line);
  GxMsg m;
  GxParseResult r = Gx_Parse(line, &m);
  if (r == GX_PARSE_EMPTY) return;
  if (!from_console) stats.rx_lines++;
  if (r != GX_PARSE_OK)
  {
    stats.rx_errors++;
    Link_Event(GX_EV_ERR, 0, 0, r == GX_PARSE_UNKNOWN ? "unknown command" : "bad arguments");
    return;
  }
  if (m.id >= GX_READY)               /* an event: not for this board */
  {
    stats.rx_errors++;
    return;
  }
  if (Player_Post(m.id, m.a) != 0) Link_Event(GX_EV_ERR, 0, 0, "busy");
}

void Link_Poll(void)
{
  uint8_t b;
  while (UartIo_Read(uart, &b))
  {
    if (Gx_LineFeed(&rx_line, (char)b)) Link_HandleLine(rx_line.line, 0);
  }
}

/* USART2 interrupt: called from stm32f4xx_it.c */
void APP_USART2_IRQ(void)
{
  UartIo_Irq(uart);
}
