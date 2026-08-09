#include "audio_input.h"

#include "codec_wm8994.h"

#define DMA_FRAMES_PER_HALF (AUDIO_INPUT_DECODE_BLOCK_SAMPLES * AUDIO_INPUT_DECIMATION)
#define DMA_WORDS_PER_HALF  (DMA_FRAMES_PER_HALF * 2U)
#define DMA_WORDS_TOTAL     (DMA_WORDS_PER_HALF * 2U)

static SAI_HandleTypeDef *s_sai_rx;
static int16_t s_dma[DMA_WORDS_TOTAL] __attribute__((aligned(32)));
static volatile uint8_t s_half_ready;
static volatile uint8_t s_full_ready;

HAL_StatusTypeDef audio_input_start(I2C_HandleTypeDef *hi2c, SAI_HandleTypeDef *hsai_rx)
{
    s_sai_rx = hsai_rx;
    if (codec_wm8994_init_line_in(hi2c) != HAL_OK) {
        return HAL_ERROR;
    }
    return HAL_SAI_Receive_DMA(s_sai_rx, (uint8_t *)s_dma, DMA_WORDS_TOTAL);
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
        HAL_SAI_Receive_DMA(s_sai_rx, (uint8_t *)s_dma, DMA_WORDS_TOTAL);
    }
}
