#include "codec_wm8994.h"

#include <stddef.h>
#include <stdio.h>

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

static HAL_StatusTypeDef codec_read(I2C_HandleTypeDef *hi2c, uint16_t reg, uint16_t *value)
{
    uint8_t data[2] = {0, 0};
    HAL_StatusTypeDef st = HAL_I2C_Mem_Read(hi2c, WM8994_ADDR, reg, I2C_MEMADD_SIZE_16BIT,
                                            data, sizeof(data), 100);
    if (st == HAL_OK) {
        *value = ((uint16_t)data[0] << 8) | data[1];
    }
    return st;
}

static void dump_reg(I2C_HandleTypeDef *hi2c, uint16_t reg)
{
    uint16_t value = 0;
    HAL_StatusTypeDef st = codec_read(hi2c, reg, &value);
    printf("[audio] wm8994 r%04x st=%d val=0x%04x\r\n", reg, st, value);
}

HAL_StatusTypeDef codec_wm8994_init_line_in(I2C_HandleTypeDef *hi2c)
{
    static const codec_reg_t init[] = {
        /* Reset and WM8994 errata workaround from ST's BSP component driver. */
        {0x0000, 0x0000},
        {0x0102, 0x0003},
        {0x0817, 0x0000},
        {0x0102, 0x0000},

        /* Bias/VMID startup. */
        {0x0039, 0x006C},
        {0x0001, 0x0013},

        /* Line IN 1 -> ADC -> AIF1ADC1 timeslot 0. */
        {0x0028, 0x0011},
        {0x0029, 0x0025},
        {0x002A, 0x0025},
        {0x0004, 0x0303},
        {0x0440, 0x00DB},
        {0x0002, 0x6350},
        {0x0606, 0x0002},
        {0x0607, 0x0002},
        {0x0700, 0x800D},

        /* AIF1: 48 kHz, 16-bit I2S, slave, MCLK1 source. */
        {0x0210, 0x0083},
        {0x0300, 0x4010},
        {0x0302, 0x0000},
        {0x0208, 0x000A},
        {0x0200, 0x0001},

        /* Input PGA and AIF1 ADC volume/filter. */
        {0x0018, 0x000B},
        {0x001A, 0x000B},
        {0x0410, 0x1800},
        {0x0400, 0x0140},
        {0x0401, 0x0140},
    };

    HAL_Delay(10);
    uint16_t id = 0;
    if (codec_read(hi2c, 0x0000, &id) == HAL_OK) {
        printf("[audio] wm8994 id before reset 0x%04x\r\n", id);
    }

    for (size_t i = 0; i < sizeof(init) / sizeof(init[0]); ++i) {
        HAL_StatusTypeDef st = codec_write(hi2c, init[i].reg, init[i].value);
        if (st != HAL_OK) {
            printf("[audio] wm8994 write reg 0x%04x failed st=%d i2c_err=0x%08lx\r\n",
                   init[i].reg, st, HAL_I2C_GetError(hi2c));
            return HAL_ERROR;
        }
        HAL_Delay(init[i].reg == 0x0001 ? 50 : 1);
    }
    printf("[audio] wm8994 line-in register init ok\r\n");
    dump_reg(hi2c, 0x0000);
    dump_reg(hi2c, 0x0001);
    dump_reg(hi2c, 0x0002);
    dump_reg(hi2c, 0x0004);
    dump_reg(hi2c, 0x0018);
    dump_reg(hi2c, 0x001A);
    dump_reg(hi2c, 0x0028);
    dump_reg(hi2c, 0x0029);
    dump_reg(hi2c, 0x002A);
    dump_reg(hi2c, 0x0210);
    dump_reg(hi2c, 0x0300);
    dump_reg(hi2c, 0x0440);
    dump_reg(hi2c, 0x0606);
    dump_reg(hi2c, 0x0607);
    return HAL_OK;
}
