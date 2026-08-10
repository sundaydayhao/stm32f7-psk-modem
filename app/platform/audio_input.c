#include "audio_input.h"

#include <stdio.h>
#include <string.h>

#include "codec_wm8994.h"

#define DMA_FRAMES_PER_HALF (AUDIO_INPUT_DECODE_BLOCK_SAMPLES * AUDIO_INPUT_DECIMATION)
#define DMA_WORDS_PER_HALF  (DMA_FRAMES_PER_HALF * 2U)
#define DMA_WORDS_TOTAL     (DMA_WORDS_PER_HALF * 2U)
#define DECODE_QUEUE_BLOCKS 64U

static SAI_HandleTypeDef *s_sai_rx;
static int16_t s_dma[DMA_WORDS_TOTAL] __attribute__((aligned(32)));
static int16_t s_queue[DECODE_QUEUE_BLOCKS][AUDIO_INPUT_DECODE_BLOCK_SAMPLES] __attribute__((aligned(32)));
static volatile uint8_t s_queue_read;
static volatile uint8_t s_queue_write;
static volatile uint8_t s_queue_count;
static volatile uint8_t s_queue_max;
static volatile uint32_t s_half_callbacks;
static volatile uint32_t s_full_callbacks;
static volatile uint32_t s_error_callbacks;
static volatile uint32_t s_read_blocks;
static volatile uint32_t s_empty_reads;
static volatile uint32_t s_overflow_blocks;
static volatile uint32_t s_last_count;
static volatile int16_t s_last_min;
static volatile int16_t s_last_max;
static volatile uint16_t s_last_abs_avg;
static volatile uint8_t s_selected_lane;
static volatile int16_t s_raw_min[2];
static volatile int16_t s_raw_max[2];
static volatile uint16_t s_raw_abs_avg[2];

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
    int16_t min = src[0];
    int16_t max = min;
    uint32_t abs_sum = 0;
    int16_t raw_min[2] = {src[0], src[1]};
    int16_t raw_max[2] = {src[0], src[1]};
    uint32_t raw_abs_sum[2] = {0, 0};
    size_t produced = 0;

    for (size_t frame = 0; frame < DMA_FRAMES_PER_HALF; ++frame) {
        const int16_t left = src[(frame * 2U) + 0U];
        const int16_t right = src[(frame * 2U) + 1U];
        const int16_t v = (int16_t)(((int32_t)left + (int32_t)right) / 2);
        if (v < min) {
            min = v;
        }
        if (v > max) {
            max = v;
        }
        abs_sum += (uint32_t)(v < 0 ? -((int32_t)v) : v);

        if (left < raw_min[0]) {
            raw_min[0] = left;
        }
        if (left > raw_max[0]) {
            raw_max[0] = left;
        }
        if (right < raw_min[1]) {
            raw_min[1] = right;
        }
        if (right > raw_max[1]) {
            raw_max[1] = right;
        }
        raw_abs_sum[0] += (uint32_t)(left < 0 ? -((int32_t)left) : left);
        raw_abs_sum[1] += (uint32_t)(right < 0 ? -((int32_t)right) : right);
    }

    for (size_t frame = 0; frame < DMA_FRAMES_PER_HALF && produced < max_samples; frame += AUDIO_INPUT_DECIMATION) {
        int32_t acc = 0;
        for (size_t k = 0; k < AUDIO_INPUT_DECIMATION; ++k) {
            const size_t idx = (frame + k) * 2U;
            acc += ((int32_t)src[idx] + (int32_t)src[idx + 1U]) / 2;
        }
        out[produced++] = (int16_t)(acc / AUDIO_INPUT_DECIMATION);
    }

    s_raw_min[0] = raw_min[0];
    s_raw_min[1] = raw_min[1];
    s_raw_max[0] = raw_max[0];
    s_raw_max[1] = raw_max[1];
    s_raw_abs_avg[0] = (uint16_t)(raw_abs_sum[0] / DMA_FRAMES_PER_HALF);
    s_raw_abs_avg[1] = (uint16_t)(raw_abs_sum[1] / DMA_FRAMES_PER_HALF);
    s_selected_lane = 0U;

    return produced;
}

static void record_samples(const int16_t *samples, size_t count)
{
    if (count == 0) {
        return;
    }

    int16_t min = samples[0];
    int16_t max = samples[0];
    uint32_t abs_sum = 0;

    for (size_t i = 0; i < count; ++i) {
        const int16_t v = samples[i];
        if (v < min) {
            min = v;
        }
        if (v > max) {
            max = v;
        }
        abs_sum += (uint32_t)(v < 0 ? -((int32_t)v) : v);
    }

    s_last_count = (uint32_t)count;
    s_last_min = min;
    s_last_max = max;
    s_last_abs_avg = (uint16_t)(abs_sum / count);
    ++s_read_blocks;
}

static void queue_dma_half(const int16_t *src)
{
    if (s_queue_count >= DECODE_QUEUE_BLOCKS) {
        ++s_overflow_blocks;
        return;
    }

    int16_t *dst = s_queue[s_queue_write];
    size_t count = decimate_half(src, dst, AUDIO_INPUT_DECODE_BLOCK_SAMPLES);
    record_samples(dst, count);

    s_queue_write = (uint8_t)((s_queue_write + 1U) % DECODE_QUEUE_BLOCKS);
    ++s_queue_count;
    if (s_queue_count > s_queue_max) {
        s_queue_max = s_queue_count;
    }
}

size_t audio_input_read(int16_t *out, size_t max_samples)
{
    uint32_t primask = __get_PRIMASK();
    __disable_irq();
    if (s_queue_count == 0U) {
        ++s_empty_reads;
        if (primask == 0U) {
            __enable_irq();
        }
        return 0;
    }
    uint8_t block = s_queue_read;
    s_queue_read = (uint8_t)((s_queue_read + 1U) % DECODE_QUEUE_BLOCKS);
    --s_queue_count;
    if (primask == 0U) {
        __enable_irq();
    }

    size_t count = AUDIO_INPUT_DECODE_BLOCK_SAMPLES;
    if (count > max_samples) {
        count = max_samples;
    }
    memcpy(out, s_queue[block], count * sizeof(out[0]));
    return count;
}

void HAL_SAI_RxHalfCpltCallback(SAI_HandleTypeDef *hsai)
{
    if (hsai == s_sai_rx) {
        ++s_half_callbacks;
        SCB_InvalidateDCache_by_Addr((uint32_t *)&s_dma[0], DMA_WORDS_PER_HALF * sizeof(s_dma[0]));
        queue_dma_half(&s_dma[0]);
    }
}

void HAL_SAI_RxCpltCallback(SAI_HandleTypeDef *hsai)
{
    if (hsai == s_sai_rx) {
        ++s_full_callbacks;
        SCB_InvalidateDCache_by_Addr((uint32_t *)&s_dma[DMA_WORDS_PER_HALF],
                                     DMA_WORDS_PER_HALF * sizeof(s_dma[0]));
        queue_dma_half(&s_dma[DMA_WORDS_PER_HALF]);
    }
}

void HAL_SAI_ErrorCallback(SAI_HandleTypeDef *hsai)
{
    if (hsai == s_sai_rx) {
        ++s_error_callbacks;
        printf("[audio] sai error callback sai_err=0x%08lx dma_err=0x%08lx\r\n",
               HAL_SAI_GetError(s_sai_rx), s_sai_rx->hdmarx ? HAL_DMA_GetError(s_sai_rx->hdmarx) : 0UL);
        HAL_SAI_Receive_DMA(s_sai_rx, (uint8_t *)s_dma, DMA_WORDS_TOTAL);
    }
}

void audio_input_debug_snapshot(audio_input_debug_t *debug)
{
    if (!debug) {
        return;
    }

    uint32_t primask = __get_PRIMASK();
    __disable_irq();
    debug->half_callbacks = s_half_callbacks;
    debug->full_callbacks = s_full_callbacks;
    debug->error_callbacks = s_error_callbacks;
    debug->read_blocks = s_read_blocks;
    debug->empty_reads = s_empty_reads;
    debug->overflow_blocks = s_overflow_blocks;
    debug->queued_blocks = s_queue_count;
    debug->max_queued_blocks = s_queue_max;
    debug->last_count = s_last_count;
    debug->last_min = s_last_min;
    debug->last_max = s_last_max;
    debug->last_abs_avg = s_last_abs_avg;
    debug->selected_lane = s_selected_lane;
    debug->raw_min[0] = s_raw_min[0];
    debug->raw_min[1] = s_raw_min[1];
    debug->raw_max[0] = s_raw_max[0];
    debug->raw_max[1] = s_raw_max[1];
    debug->raw_abs_avg[0] = s_raw_abs_avg[0];
    debug->raw_abs_avg[1] = s_raw_abs_avg[1];
    if (primask == 0U) {
        __enable_irq();
    }

    debug->sai_state = s_sai_rx ? (uint32_t)s_sai_rx->State : 0xFFFFFFFFUL;
    debug->sai_error = s_sai_rx ? HAL_SAI_GetError(s_sai_rx) : 0xFFFFFFFFUL;

    DMA_HandleTypeDef *hdma = s_sai_rx ? s_sai_rx->hdmarx : NULL;
    debug->dma_state = hdma ? (uint32_t)hdma->State : 0xFFFFFFFFUL;
    debug->dma_error = hdma ? HAL_DMA_GetError(hdma) : 0xFFFFFFFFUL;
    debug->dma_cr = hdma ? hdma->Instance->CR : 0UL;
    debug->dma_ndtr = hdma ? hdma->Instance->NDTR : 0UL;
}
