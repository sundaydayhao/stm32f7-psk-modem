#pragma once

#include "spectrum.h"
#include "stm32f7xx_hal.h"

#define DISPLAY_WIDTH 800U
#define DISPLAY_HEIGHT 480U
#define DISPLAY_FRAMEBUFFER 0xC0000000U

void display_init(LTDC_HandleTypeDef *hltdc, DSI_HandleTypeDef *hdsi);
void display_task(void);
void display_status(const char *line1, const char *line2);
void display_render(const spectrum_t *spectrum, const char *mode, int tone_hz, const char *decoded);
