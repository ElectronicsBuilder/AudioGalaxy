/*
 * link.h - the serial link to BeaconGalaxy (USART2, GALAXY* protocol).
 * Lines received are parsed and posted to the player; player events are
 * formatted and sent back. See galaxy_link.h and docs/protocol.md.
 */
#ifndef APP_LINK_H
#define APP_LINK_H

#include "galaxy_link.h"

void Link_Init(void);
void Link_Poll(void);                      /* App task: drain received bytes */
void Link_HandleLine(char *line, int from_console);   /* also used by the console */
void Link_Event(GxId id, long a, long b, const char *text);   /* player events (any task) */
void Link_SetLog(int on);                  /* log every line on the console */
int  Link_GetLog(void);

typedef struct {
  unsigned long rx_lines, tx_lines, rx_errors, rx_overruns;
} LinkStats;
void Link_GetStats(LinkStats *out);

#endif
