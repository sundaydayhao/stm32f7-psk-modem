#include "main.h"
#include "stm32f7xx_it.h"

extern DMA_HandleTypeDef hdma_sai1_b_rx;
extern LTDC_HandleTypeDef hltdc;
extern DSI_HandleTypeDef hdsi;
extern TIM_HandleTypeDef htim6;

void NMI_Handler(void) { while (1) {} }
void HardFault_Handler(void) { while (1) {} }
void MemManage_Handler(void) { while (1) {} }
void BusFault_Handler(void) { while (1) {} }
void UsageFault_Handler(void) { while (1) {} }
void DebugMon_Handler(void) {}

void TIM6_DAC_IRQHandler(void)
{
    HAL_TIM_IRQHandler(&htim6);
}

void DMA2_Stream4_IRQHandler(void)
{
    HAL_DMA_IRQHandler(&hdma_sai1_b_rx);
}

void LTDC_IRQHandler(void)
{
    HAL_LTDC_IRQHandler(&hltdc);
}

void DSI_IRQHandler(void)
{
    HAL_DSI_IRQHandler(&hdsi);
}
