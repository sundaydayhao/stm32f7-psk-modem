#ifndef __STM32F7xx_HAL_CONF_H
#define __STM32F7xx_HAL_CONF_H

#ifdef __cplusplus
extern "C" {
#endif

#define HAL_MODULE_ENABLED
#define HAL_CORTEX_MODULE_ENABLED
#define HAL_DMA_MODULE_ENABLED
#define HAL_DMA2D_MODULE_ENABLED
#define HAL_DSI_MODULE_ENABLED
#define HAL_FLASH_MODULE_ENABLED
#define HAL_GPIO_MODULE_ENABLED
#define HAL_I2C_MODULE_ENABLED
#define HAL_LTDC_MODULE_ENABLED
#define HAL_PWR_MODULE_ENABLED
#define HAL_RCC_MODULE_ENABLED
#define HAL_SAI_MODULE_ENABLED
#define HAL_SDRAM_MODULE_ENABLED
#define HAL_TIM_MODULE_ENABLED
#define HAL_UART_MODULE_ENABLED

#if !defined(HSE_VALUE)
#define HSE_VALUE 25000000U
#endif
#if !defined(HSE_STARTUP_TIMEOUT)
#define HSE_STARTUP_TIMEOUT 100U
#endif
#if !defined(HSI_VALUE)
#define HSI_VALUE 16000000U
#endif
#if !defined(LSI_VALUE)
#define LSI_VALUE 32000U
#endif
#if !defined(LSE_VALUE)
#define LSE_VALUE 32768U
#endif
#if !defined(LSE_STARTUP_TIMEOUT)
#define LSE_STARTUP_TIMEOUT 5000U
#endif
#if !defined(EXTERNAL_CLOCK_VALUE)
#define EXTERNAL_CLOCK_VALUE 12288000U
#endif

#define VDD_VALUE 3300U
#define TICK_INT_PRIORITY 0U
#define USE_RTOS 0U
#define PREFETCH_ENABLE 1U
#define ART_ACCELERATOR_ENABLE 1U

#define USE_HAL_CORTEX_REGISTER_CALLBACKS 0U
#define USE_HAL_DMA_REGISTER_CALLBACKS 0U
#define USE_HAL_DMA2D_REGISTER_CALLBACKS 0U
#define USE_HAL_DSI_REGISTER_CALLBACKS 0U
#define USE_HAL_I2C_REGISTER_CALLBACKS 0U
#define USE_HAL_LTDC_REGISTER_CALLBACKS 0U
#define USE_HAL_SAI_REGISTER_CALLBACKS 0U
#define USE_HAL_SDRAM_REGISTER_CALLBACKS 0U
#define USE_HAL_TIM_REGISTER_CALLBACKS 0U

#ifdef HAL_RCC_MODULE_ENABLED
#include "stm32f7xx_hal_rcc.h"
#endif
#ifdef HAL_GPIO_MODULE_ENABLED
#include "stm32f7xx_hal_gpio.h"
#endif
#ifdef HAL_DMA_MODULE_ENABLED
#include "stm32f7xx_hal_dma.h"
#endif
#ifdef HAL_CORTEX_MODULE_ENABLED
#include "stm32f7xx_hal_cortex.h"
#endif
#ifdef HAL_DMA2D_MODULE_ENABLED
#include "stm32f7xx_hal_dma2d.h"
#endif
#ifdef HAL_DSI_MODULE_ENABLED
#include "stm32f7xx_hal_dsi.h"
#endif
#ifdef HAL_FLASH_MODULE_ENABLED
#include "stm32f7xx_hal_flash.h"
#endif
#ifdef HAL_I2C_MODULE_ENABLED
#include "stm32f7xx_hal_i2c.h"
#endif
#ifdef HAL_LTDC_MODULE_ENABLED
#include "stm32f7xx_hal_ltdc.h"
#endif
#ifdef HAL_PWR_MODULE_ENABLED
#include "stm32f7xx_hal_pwr.h"
#endif
#ifdef HAL_SAI_MODULE_ENABLED
#include "stm32f7xx_hal_sai.h"
#endif
#ifdef HAL_SDRAM_MODULE_ENABLED
#include "stm32f7xx_hal_sdram.h"
#endif
#ifdef HAL_TIM_MODULE_ENABLED
#include "stm32f7xx_hal_tim.h"
#endif
#ifdef HAL_UART_MODULE_ENABLED
#include "stm32f7xx_hal_uart.h"
#endif

#ifdef USE_FULL_ASSERT
#define assert_param(expr) ((expr) ? (void)0U : assert_failed((uint8_t *)__FILE__, __LINE__))
void assert_failed(uint8_t *file, uint32_t line);
#else
#define assert_param(expr) ((void)0U)
#endif

#ifdef __cplusplus
}
#endif

#endif
