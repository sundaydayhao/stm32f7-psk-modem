#include "main.h"

DMA_HandleTypeDef hdma_sai1_b_rx;

void HAL_MspInit(void)
{
    __HAL_RCC_PWR_CLK_ENABLE();
    __HAL_RCC_SYSCFG_CLK_ENABLE();
}

void HAL_I2C_MspInit(I2C_HandleTypeDef *hi2c)
{
    GPIO_InitTypeDef gpio = {0};
    RCC_PeriphCLKInitTypeDef pclk = {0};
    if (hi2c->Instance != I2C4) {
        return;
    }

    pclk.PeriphClockSelection = RCC_PERIPHCLK_I2C4;
    pclk.I2c4ClockSelection = RCC_I2C4CLKSOURCE_PCLK1;
    HAL_RCCEx_PeriphCLKConfig(&pclk);

    __HAL_RCC_GPIOB_CLK_ENABLE();
    __HAL_RCC_GPIOD_CLK_ENABLE();
    __HAL_RCC_I2C4_CLK_ENABLE();

    gpio.Pin = AUDIO_SDA_Pin;
    gpio.Mode = GPIO_MODE_AF_OD;
    gpio.Pull = GPIO_PULLUP;
    gpio.Speed = GPIO_SPEED_FREQ_VERY_HIGH;
    gpio.Alternate = GPIO_AF11_I2C4;
    HAL_GPIO_Init(AUDIO_SDA_GPIO_Port, &gpio);

    gpio.Pin = AUDIO_SCL_Pin;
    gpio.Alternate = GPIO_AF4_I2C4;
    HAL_GPIO_Init(AUDIO_SCL_GPIO_Port, &gpio);
}

void HAL_SAI_MspInit(SAI_HandleTypeDef *hsai)
{
    GPIO_InitTypeDef gpio = {0};
    __HAL_RCC_SAI1_CLK_ENABLE();
    __HAL_RCC_GPIOE_CLK_ENABLE();
    __HAL_RCC_GPIOG_CLK_ENABLE();
    __HAL_RCC_DMA2_CLK_ENABLE();

    if (hsai->Instance == SAI1_Block_A) {
        gpio.Pin = SAI1_FSA_Pin | SAI1_SCKA_Pin | SAI1_SDA_Pin;
        gpio.Mode = GPIO_MODE_AF_PP;
        gpio.Pull = GPIO_NOPULL;
        gpio.Speed = GPIO_SPEED_FREQ_VERY_HIGH;
        gpio.Alternate = GPIO_AF6_SAI1;
        HAL_GPIO_Init(GPIOE, &gpio);

        gpio.Pin = SAI1_MCLKA_Pin;
        HAL_GPIO_Init(SAI1_MCLKA_GPIO_Port, &gpio);
    } else if (hsai->Instance == SAI1_Block_B) {
        gpio.Pin = SAI1_SDB_Pin;
        gpio.Mode = GPIO_MODE_AF_PP;
        gpio.Pull = GPIO_NOPULL;
        gpio.Speed = GPIO_SPEED_FREQ_VERY_HIGH;
        gpio.Alternate = GPIO_AF6_SAI1;
        HAL_GPIO_Init(SAI1_SDB_GPIO_Port, &gpio);

        hdma_sai1_b_rx.Instance = DMA2_Stream5;
        hdma_sai1_b_rx.Init.Channel = DMA_CHANNEL_0;
        hdma_sai1_b_rx.Init.Direction = DMA_PERIPH_TO_MEMORY;
        hdma_sai1_b_rx.Init.PeriphInc = DMA_PINC_DISABLE;
        hdma_sai1_b_rx.Init.MemInc = DMA_MINC_ENABLE;
        hdma_sai1_b_rx.Init.PeriphDataAlignment = DMA_PDATAALIGN_HALFWORD;
        hdma_sai1_b_rx.Init.MemDataAlignment = DMA_MDATAALIGN_HALFWORD;
        hdma_sai1_b_rx.Init.Mode = DMA_CIRCULAR;
        hdma_sai1_b_rx.Init.Priority = DMA_PRIORITY_VERY_HIGH;
        hdma_sai1_b_rx.Init.FIFOMode = DMA_FIFOMODE_DISABLE;
        if (HAL_DMA_Init(&hdma_sai1_b_rx) != HAL_OK) {
            Error_Handler();
        }
        __HAL_LINKDMA(hsai, hdmarx, hdma_sai1_b_rx);
        HAL_NVIC_SetPriority(DMA2_Stream5_IRQn, 5, 0);
        HAL_NVIC_EnableIRQ(DMA2_Stream5_IRQn);
    }
}

void HAL_LTDC_MspInit(LTDC_HandleTypeDef *hltdc)
{
    if (hltdc->Instance == LTDC) {
        __HAL_RCC_LTDC_CLK_ENABLE();
        HAL_NVIC_SetPriority(LTDC_IRQn, 5, 0);
        HAL_NVIC_EnableIRQ(LTDC_IRQn);
    }
}

void HAL_DSI_MspInit(DSI_HandleTypeDef *hdsi)
{
    if (hdsi->Instance == DSI) {
        __HAL_RCC_DSI_CLK_ENABLE();
        HAL_NVIC_SetPriority(DSI_IRQn, 5, 0);
        HAL_NVIC_EnableIRQ(DSI_IRQn);
    }
}

void HAL_DMA2D_MspInit(DMA2D_HandleTypeDef *hdma2d)
{
    if (hdma2d->Instance == DMA2D) {
        __HAL_RCC_DMA2D_CLK_ENABLE();
    }
}

static void FMC_MspInit(void)
{
    static uint8_t initialized;
    GPIO_InitTypeDef gpio = {0};
    if (initialized) {
        return;
    }
    initialized = 1;

    __HAL_RCC_FMC_CLK_ENABLE();
    __HAL_RCC_GPIOD_CLK_ENABLE();
    __HAL_RCC_GPIOE_CLK_ENABLE();
    __HAL_RCC_GPIOF_CLK_ENABLE();
    __HAL_RCC_GPIOG_CLK_ENABLE();
    __HAL_RCC_GPIOH_CLK_ENABLE();
    __HAL_RCC_GPIOI_CLK_ENABLE();

    gpio.Mode = GPIO_MODE_AF_PP;
    gpio.Pull = GPIO_NOPULL;
    gpio.Speed = GPIO_SPEED_FREQ_VERY_HIGH;
    gpio.Alternate = GPIO_AF12_FMC;

    gpio.Pin = FMC_D0_Pin | FMC_D1_Pin | FMC_D2_Pin | FMC_D3_Pin | FMC_D13_Pin |
               FMC_D14_Pin | FMC_D15_Pin;
    HAL_GPIO_Init(GPIOD, &gpio);
    gpio.Pin = FMC_NBL0_Pin | FMC_NBL1_Pin | FMC_D4_Pin | FMC_D5_Pin | FMC_D6_Pin |
               FMC_D7_Pin | FMC_D8_Pin | FMC_D9_Pin | FMC_D10_Pin | FMC_D11_Pin | FMC_D12_Pin;
    HAL_GPIO_Init(GPIOE, &gpio);
    gpio.Pin = FMC_A0_Pin | FMC_A1_Pin | FMC_A2_Pin | FMC_A3_Pin | FMC_A4_Pin |
               FMC_A5_Pin | FMC_A6_Pin | FMC_A7_Pin | FMC_A8_Pin | FMC_A9_Pin | FMC_SDNRAS_Pin;
    HAL_GPIO_Init(GPIOF, &gpio);
    gpio.Pin = FMC_A10_Pin | FMC_A11_Pin | FMC_A12_Pin | FMC_BA0_Pin | FMC_BA1_Pin |
               FMC_SDCLK_Pin | FMC_SDNCAS_Pin;
    HAL_GPIO_Init(GPIOG, &gpio);
    gpio.Pin = FMC_SDCKE0_Pin | FMC_SDNE0_Pin | FMC_SDNME_Pin | D20_Pin | D21_Pin |
               D22_Pin | D23_Pin | FMC_D16_Pin | FMC_D18_Pin | FMC_D19_Pin;
    HAL_GPIO_Init(GPIOH, &gpio);
    gpio.Pin = D24_Pin | D25_Pin | D26_Pin | D27_Pin | D28_Pin | D29_Pin |
               D30_Pin | D31_Pin | FMC_NBL2_Pin | FMC_NBL3_Pin;
    HAL_GPIO_Init(GPIOI, &gpio);
}

void HAL_SDRAM_MspInit(SDRAM_HandleTypeDef *hsdram)
{
    FMC_MspInit();
}
