#pragma once

#include "stm32f7xx_hal.h"

HAL_StatusTypeDef codec_wm8994_init_line_in(I2C_HandleTypeDef *hi2c);
