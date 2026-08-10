#pragma once

#include <stddef.h>
#include <stdint.h>

#include "stm32f7xx_hal.h"

#define AUDIO_INPUT_CODEC_RATE_HZ 48000
#define AUDIO_INPUT_DECODE_RATE_HZ 8000
#define AUDIO_INPUT_DECIMATION 6
#define AUDIO_INPUT_DECODE_BLOCK_SAMPLES 128

HAL_StatusTypeDef audio_input_start(I2C_HandleTypeDef *hi2c, SAI_HandleTypeDef *hsai_clk, SAI_HandleTypeDef *hsai_rx);
size_t audio_input_read(int16_t *out, size_t max_samples);
