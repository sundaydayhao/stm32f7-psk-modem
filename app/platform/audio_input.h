#pragma once

#include <stddef.h>
#include <stdint.h>

#include "stm32f7xx_hal.h"

#define AUDIO_INPUT_CODEC_RATE_HZ 49152
#define AUDIO_INPUT_DECODE_RATE_HZ 8192
#define AUDIO_INPUT_DECIMATION 6
#define AUDIO_INPUT_DECODE_BLOCK_SAMPLES 128

typedef struct {
    uint32_t half_callbacks;
    uint32_t full_callbacks;
    uint32_t error_callbacks;
    uint32_t read_blocks;
    uint32_t empty_reads;
    uint32_t overflow_blocks;
    uint32_t queued_blocks;
    uint32_t max_queued_blocks;
    uint32_t last_count;
    int16_t last_min;
    int16_t last_max;
    uint16_t last_abs_avg;
    uint8_t selected_lane;
    int16_t raw_min[2];
    int16_t raw_max[2];
    uint16_t raw_abs_avg[2];
    uint32_t sai_state;
    uint32_t dma_state;
    uint32_t sai_error;
    uint32_t dma_error;
    uint32_t dma_cr;
    uint32_t dma_ndtr;
} audio_input_debug_t;

HAL_StatusTypeDef audio_input_start(I2C_HandleTypeDef *hi2c, SAI_HandleTypeDef *hsai_clk, SAI_HandleTypeDef *hsai_rx);
size_t audio_input_read(int16_t *out, size_t max_samples);
void audio_input_debug_snapshot(audio_input_debug_t *debug);
