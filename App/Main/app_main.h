/*
 * app_main.h - AudioGalaxy application entry points (called from freertos.c).
 */
#ifndef APP_MAIN_H
#define APP_MAIN_H

void App_Init(void);
void App_Run(void);          /* never returns */

void APP_USART2_IRQ(void);   /* link */
void APP_USART3_IRQ(void);   /* console */

#endif
