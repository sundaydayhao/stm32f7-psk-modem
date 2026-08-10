#include "audio_input.h"

#include <stdio.h>

#include "codec_wm8994.h"

#define DMA_FRAMES_PER_HALF (AUDIO_INPUT_DECODE_BLOCK_SAMPLES * AUDIO_INPUT_DECIMATION)
#define DMA_WORDS_PER_HALF  (DMA_FRAMES_PER_HALF * 2U)
#define DMA_WORDS_TOTAL     (DMA_WORDS_PER_HALF * 2U)

static SAI_HandleTypeDef *s_sai_rx;
static int16_t s_dma[DMA_WORDS_TOTAL] __attribute__((aligned(32)));
static volatile uint8_t s_half_ready;
static volatile uint8_t s_full_ready;

HAL_StatusTypeDef audio_input_start(I2C_HandleTypeDef *hi2c, SAI_HandleTypeDef *hsai_clk, SAI_HandleTypeDef *hsai_rx)
{
    s_sai_rx = hsai_rx;
    printf("[audio] init begin\r\n");
    if (codec_wm8994_init_line_in(hi2c) != HAL_OK) {
        printf("[audio] codec init failed\r\n");
        return HAL_ERROR;
    }
    __HAL_SAI_ENABLE(hsai_clk);
    DMA_HandleTypeDef *hdma = s_sai_rx->hdmarx;
    printf("[audio] pre dma sai_state=%lu sai_lock=%lu hdmarx=%p dma_state=%lu dma_lock=%lu dma_inst=%p size=%lu cr=0x%08lx ndtr=%lu\r\n",
           (uint32_t)s_sai_rx->State,
           (uint32_t)s_sai_rx->Lock,
           (void *)hdma,
           hdma ? (uint32_t)hdma->State : 0xFFFFFFFFUL,
           hdma ? (uint32_t)hdma->Lock : 0xFFFFFFFFUL,
           hdma ? (void *)hdma->Instance : NULL,
           (uint32_t)DMA_WORDS_TOTAL,
           hdma ? hdma->Instance->CR : 0UL,
           hdma ? hdma->Instance->NDTR : 0UL);
    HAL_StatusTypeDef st = HAL_SAI_Receive_DMA(s_sai_rx, (uint8_t *)s_dma, DMA_WORDS_TOTAL);
    printf("[audio] sai rx dma start st=%d sai_state=%lu dma_state=%lu sai_err=0x%08lx dma_err=0x%08lx cr=0x%08lx ndtr=%lu\r\n",
           st,
           (uint32_t)s_sai_rx->State,
           hdma ? (uint32_t)hdma->State : 0xFFFFFFFFUL,
           HAL_SAI_GetError(s_sai_rx),
           hdma ? HAL_DMA_GetError(hdma) : 0UL,
           hdma ? hdma->Instance->CR : 0UL,
           hdma ? hdma->Instance->NDTR : 0UL);
    return st;
}

static size_t decimate_half(const int16_t *src, int16_t *out, size_t max_samples)
{
    size_t produced = 0;
    for (size_t frame = 0; frame < DMA_FRAMES_PER_HALF && produced < max_samples; frame += AUDIO_INPUT_DECIMATION) {
        int32_t acc = 0;
        for (size_t k = 0; k < AUDIO_INPUT_DECIMATION; ++k) {
            acc += src[(frame + k) * 2U];
        }
        out[produced++] = (int16_t)(acc / AUDIO_INPUT_DECIMATION);
    }
    return produced;
}

size_t audio_input_read(int16_t *out, size_t max_samples)
{
    if (s_half_ready) {
        s_half_ready = 0;
        SCB_InvalidateDCache_by_Addr((uint32_t *)&s_dma[0], DMA_WORDS_PER_HALF * sizeof(s_dma[0]));
        return decimate_half(&s_dma[0], out, max_samples);
    }
    if (s_full_ready) {
        s_full_ready = 0;
        SCB_InvalidateDCache_by_Addr((uint32_t *)&s_dma[DMA_WORDS_PER_HALF],
                                     DMA_WORDS_PER_HALF * sizeof(s_dma[0]));
        return decimate_half(&s_dma[DMA_WORDS_PER_HALF], out, max_samples);
    }
    return 0;
}

void HAL_SAI_RxHalfCpltCallback(SAI_HandleTypeDef *hsai)
{
    if (hsai == s_sai_rx) {
        s_half_ready = 1;
    }
}

void HAL_SAI_RxCpltCallback(SAI_HandleTypeDef *hsai)
{
    if (hsai == s_sai_rx) {
        s_full_ready = 1;
    }
}

void HAL_SAI_ErrorCallback(SAI_HandleTypeDef *hsai)
{
    if (hsai == s_sai_rx) {
        printf("[audio] sai error callback sai_err=0x%08lx dma_err=0x%08lx\r\n",
               HAL_SAI_GetError(s_sai_rx), s_sai_rx->hdmarx ? HAL_DMA_GetError(s_sai_rx->hdmarx) : 0UL);
        HAL_SAI_Receive_DMA(s_sai_rx, (uint8_t *)s_dma, DMA_WORDS_TOTAL);
    }
}
