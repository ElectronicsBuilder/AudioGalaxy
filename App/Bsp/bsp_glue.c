/*
 * bsp_glue.c - interrupt handlers and callbacks of the Discovery audio BSP.
 * CubeMX does not manage I2S2/I2S3 (the BSP does), so their DMA interrupt
 * handlers live here. Priorities (0x0E / 0x0F) are set by the BSP and are
 * below configMAX_SYSCALL_INTERRUPT_PRIORITY, so the callbacks may use the RTOS.
 */
#include "stm32f4_discovery_audio.h"
#include "player.h"

extern I2S_HandleTypeDef hAudioOutI2s;
extern I2S_HandleTypeDef hAudioInI2s;

void I2S3_IRQHandler(void)          /* = DMA1_Stream7_IRQHandler (codec out) */
{
  HAL_DMA_IRQHandler(hAudioOutI2s.hdmatx);
}

void I2S2_IRQHandler(void)          /* = DMA1_Stream3_IRQHandler (microphone) */
{
  HAL_DMA_IRQHandler(hAudioInI2s.hdmarx);
}

void BSP_AUDIO_OUT_HalfTransfer_CallBack(void)  { Player_IsrOutHalf(); }
void BSP_AUDIO_OUT_TransferComplete_CallBack(void) { Player_IsrOutFull(); }
void BSP_AUDIO_OUT_Error_CallBack(void)         { Player_IsrOutError(); }
void BSP_AUDIO_IN_HalfTransfer_CallBack(void)   { Player_IsrInHalf(); }
void BSP_AUDIO_IN_TransferComplete_CallBack(void) { Player_IsrInFull(); }
