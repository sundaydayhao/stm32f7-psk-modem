#include "codec_wm8994.h"

#include <stddef.h>

#define WM8994_ADDR (0x34U)

typedef struct {
    uint16_t reg;
    uint16_t value;
} codec_reg_t;

static HAL_StatusTypeDef codec_write(I2C_HandleTypeDef *hi2c, uint16_t reg, uint16_t value)
{
    uint8_t data[2] = {(uint8_t)(value >> 8), (uint8_t)value};
    return HAL_I2C_Mem_Write(hi2c, WM8994_ADDR, reg, I2C_MEMADD_SIZE_16BIT,
                             data, sizeof(data), 100);
}

HAL_StatusTypeDef codec_wm8994_init_line_in(I2C_HandleTypeDef *hi2c)
{
    static const codec_reg_t init[] = {
        {0x0000, 0x0000},
        {0x0001, 0x0003},
        {0x0002, 0x6000},
        {0x0004, 0x0303},
        {0x0005, 0x0303},
        {0x0018, 0x000B},
        {0x0019, 0x000B},
        {0x0028, 0x0030},
        {0x0029, 0x0030},
        {0x0200, 0x0001},
        {0x0204, 0x0001},
        {0x0208, 0x000A},
        {0x0209, 0x000A},
        {0x0300, 0x4010},
        {0x0302, 0x7000},
        {0x0303, 0x7000},
        {0x0400, 0x0010},
        {0x0401, 0x0000},
        {0x0601, 0x0005},
        {0x0602, 0x0005},
        {0x0610, 0x01C0},
        {0x0611, 0x01C0},
    };

    HAL_Delay(10);
    for (size_t i = 0; i < sizeof(init) / sizeof(init[0]); ++i) {
        if (codec_write(hi2c, init[i].reg, init[i].value) != HAL_OK) {
            return HAL_ERROR;
        }
        HAL_Delay(1);
    }
    return HAL_OK;
}
