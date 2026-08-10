#include "main.h"

#include <stdio.h>
#include <string.h>

#include "audio_input.h"
#include "display.h"
#include "psk_modem.h"
#include "spectrum.h"

I2C_HandleTypeDef hi2c4;
SAI_HandleTypeDef hsai_BlockA1;
SAI_HandleTypeDef hsai_BlockB1;
LTDC_HandleTypeDef hltdc;
DSI_HandleTypeDef hdsi;
DMA2D_HandleTypeDef hdma2d;
SDRAM_HandleTypeDef hsdram1;
UART_HandleTypeDef huart1;

static psk_modem_t g_modem;
static char g_text[1024];
static volatile uint8_t g_trace_ready;

#define LCD_CLOCK_KHZ 27429U
#define DSI_LANE_BYTE_CLOCK_KHZ 62500U
#define LCD_HSA 2U
#define LCD_HBP 34U
#define LCD_HFP 34U
#define LCD_VSA 1U
#define LCD_VBP 15U
#define LCD_VFP 16U

void SystemClock_Config(void);
void PeriphCommonClock_Config(void);
static void MPU_Config(void);
static void MX_GPIO_Init(void);
static void MX_USART1_UART_Init(void);
static void MX_I2C4_Init(void);
static void MX_SAI1_Init(void);
static void MX_LTDC_Init(void);
static void MX_DSI_Init(void);
static void MX_DMA2D_Init(void);
static void MX_FMC_Init(void);
static void SDRAM_Startup(void);

int main(void)
{
    MPU_Config();
    SCB_EnableICache();
    SCB_EnableDCache();
    HAL_Init();
    SystemClock_Config();
    PeriphCommonClock_Config();

    MX_GPIO_Init();
    MX_USART1_UART_Init();
    printf("\r\n[boot] stm32f7-psk-modem trace online\r\n");
    MX_I2C4_Init();
    printf("[boot] i2c4 ok\r\n");
    MX_FMC_Init();
    SDRAM_Startup();
    printf("[boot] sdram ok\r\n");
    MX_DMA2D_Init();
    MX_DSI_Init();
    printf("[boot] dsi configured\r\n");
    MX_LTDC_Init();
    printf("[boot] ltdc started\r\n");
    MX_SAI1_Init();
    printf("[boot] sai1 ok\r\n");

    display_init(&hltdc, &hdsi);
    display_status("STM32F769I PSK modem", "Line IN -> WM8994 -> SAI1B DMA");
    printf("[boot] lvgl/display init done\r\n");

    psk_modem_config_t cfg = {
        .mode = PSK_MODE_BPSK31,
        .sample_rate_hz = AUDIO_INPUT_DECODE_RATE_HZ,
        .rx_tone_hz = 1500,
        .squelch = 35,
    };
    psk_modem_init(&g_modem, &cfg);

    if (audio_input_start(&hi2c4, &hsai_BlockA1, &hsai_BlockB1) != HAL_OK) {
        printf("[error] audio init failed\r\n");
        display_status("Audio init failed", "Check WM8994/I2C4/SAI1B");
    } else {
        printf("[boot] audio capture started\r\n");
    }

    int16_t samples[AUDIO_INPUT_DECODE_BLOCK_SAMPLES];
    char decoded[96];
    spectrum_t spectrum;
    g_text[0] = '\0';

    while (1) {
        size_t count = audio_input_read(samples, AUDIO_INPUT_DECODE_BLOCK_SAMPLES);
        if (count == 0) {
            display_task();
            continue;
        }

        size_t got = psk_modem_process(&g_modem, samples, count, decoded, sizeof(decoded));
        if (got > 0) {
            size_t old = strlen(g_text);
            if (old + got >= sizeof(g_text)) {
                memmove(g_text, g_text + sizeof(g_text) / 2, sizeof(g_text) / 2);
                g_text[sizeof(g_text) / 2] = '\0';
                old = strlen(g_text);
            }
            strncat(g_text, decoded, sizeof(g_text) - old - 1);
        }

        spectrum_analyze(samples, count, AUDIO_INPUT_DECODE_RATE_HZ, &spectrum);
        display_render(&spectrum, psk_modem_mode_name(psk_modem_mode(&g_modem)),
                       psk_modem_tone_hz(&g_modem), g_text);
    }
}

void SystemClock_Config(void)
{
    RCC_OscInitTypeDef osc = {0};
    RCC_ClkInitTypeDef clk = {0};

    HAL_PWR_EnableBkUpAccess();
    __HAL_RCC_PWR_CLK_ENABLE();
    __HAL_PWR_VOLTAGESCALING_CONFIG(PWR_REGULATOR_VOLTAGE_SCALE1);

    osc.OscillatorType = RCC_OSCILLATORTYPE_HSE;
    osc.HSEState = RCC_HSE_ON;
    osc.PLL.PLLState = RCC_PLL_ON;
    osc.PLL.PLLSource = RCC_PLLSOURCE_HSE;
    osc.PLL.PLLM = 25;
    osc.PLL.PLLN = 432;
    osc.PLL.PLLP = RCC_PLLP_DIV2;
    osc.PLL.PLLQ = 9;
    if (HAL_RCC_OscConfig(&osc) != HAL_OK) {
        Error_Handler();
    }
    if (HAL_PWREx_EnableOverDrive() != HAL_OK) {
        Error_Handler();
    }

    clk.ClockType = RCC_CLOCKTYPE_HCLK | RCC_CLOCKTYPE_SYSCLK |
                    RCC_CLOCKTYPE_PCLK1 | RCC_CLOCKTYPE_PCLK2;
    clk.SYSCLKSource = RCC_SYSCLKSOURCE_PLLCLK;
    clk.AHBCLKDivider = RCC_SYSCLK_DIV1;
    clk.APB1CLKDivider = RCC_HCLK_DIV4;
    clk.APB2CLKDivider = RCC_HCLK_DIV2;
    if (HAL_RCC_ClockConfig(&clk, FLASH_LATENCY_7) != HAL_OK) {
        Error_Handler();
    }
}

void PeriphCommonClock_Config(void)
{
    RCC_PeriphCLKInitTypeDef pclk = {0};
    pclk.PeriphClockSelection = RCC_PERIPHCLK_LTDC | RCC_PERIPHCLK_SAI1;
    pclk.PLLSAI.PLLSAIN = 384;
    pclk.PLLSAI.PLLSAIR = 7;
    pclk.PLLSAI.PLLSAIQ = 2;
    pclk.PLLSAI.PLLSAIP = RCC_PLLSAIP_DIV4;
    pclk.PLLSAIDivR = RCC_PLLSAIDIVR_2;
    pclk.Sai1ClockSelection = RCC_SAI1CLKSOURCE_PLLSAI;
    if (HAL_RCCEx_PeriphCLKConfig(&pclk) != HAL_OK) {
        Error_Handler();
    }
}

static void MX_I2C4_Init(void)
{
    hi2c4.Instance = I2C4;
    hi2c4.Init.Timing = 0x40912732;
    hi2c4.Init.OwnAddress1 = 0;
    hi2c4.Init.AddressingMode = I2C_ADDRESSINGMODE_7BIT;
    hi2c4.Init.DualAddressMode = I2C_DUALADDRESS_DISABLE;
    hi2c4.Init.OwnAddress2 = 0;
    hi2c4.Init.OwnAddress2Masks = I2C_OA2_NOMASK;
    hi2c4.Init.GeneralCallMode = I2C_GENERALCALL_DISABLE;
    hi2c4.Init.NoStretchMode = I2C_NOSTRETCH_DISABLE;
    if (HAL_I2C_Init(&hi2c4) != HAL_OK) {
        Error_Handler();
    }
    HAL_I2CEx_ConfigAnalogFilter(&hi2c4, I2C_ANALOGFILTER_ENABLE);
    HAL_I2CEx_ConfigDigitalFilter(&hi2c4, 0);
}

static void MX_SAI1_Init(void)
{
    hsai_BlockA1.Instance = SAI1_Block_A;
    hsai_BlockA1.Init.Protocol = SAI_FREE_PROTOCOL;
    hsai_BlockA1.Init.AudioMode = SAI_MODEMASTER_RX;
    hsai_BlockA1.Init.DataSize = SAI_DATASIZE_16;
    hsai_BlockA1.Init.FirstBit = SAI_FIRSTBIT_MSB;
    hsai_BlockA1.Init.ClockStrobing = SAI_CLOCKSTROBING_FALLINGEDGE;
    hsai_BlockA1.Init.Synchro = SAI_ASYNCHRONOUS;
    hsai_BlockA1.Init.OutputDrive = SAI_OUTPUTDRIVE_ENABLE;
    hsai_BlockA1.Init.NoDivider = SAI_MASTERDIVIDER_ENABLE;
    hsai_BlockA1.Init.FIFOThreshold = SAI_FIFOTHRESHOLD_1QF;
    hsai_BlockA1.Init.AudioFrequency = SAI_AUDIO_FREQUENCY_48K;
    hsai_BlockA1.Init.MonoStereoMode = SAI_STEREOMODE;
    hsai_BlockA1.Init.CompandingMode = SAI_NOCOMPANDING;
    hsai_BlockA1.Init.TriState = SAI_OUTPUT_NOTRELEASED;
    hsai_BlockA1.FrameInit.FrameLength = 64;
    hsai_BlockA1.FrameInit.ActiveFrameLength = 32;
    hsai_BlockA1.FrameInit.FSDefinition = SAI_FS_CHANNEL_IDENTIFICATION;
    hsai_BlockA1.FrameInit.FSPolarity = SAI_FS_ACTIVE_LOW;
    hsai_BlockA1.FrameInit.FSOffset = SAI_FS_BEFOREFIRSTBIT;
    hsai_BlockA1.SlotInit.FirstBitOffset = 0;
    hsai_BlockA1.SlotInit.SlotSize = SAI_SLOTSIZE_DATASIZE;
    hsai_BlockA1.SlotInit.SlotNumber = 4;
    hsai_BlockA1.SlotInit.SlotActive = SAI_SLOTACTIVE_0 | SAI_SLOTACTIVE_2;
    if (HAL_SAI_Init(&hsai_BlockA1) != HAL_OK) {
        Error_Handler();
    }

    SAI_InitTypeDef sai1_common_init = hsai_BlockA1.Init;
    SAI_FrameInitTypeDef sai1_common_frame = hsai_BlockA1.FrameInit;
    SAI_SlotInitTypeDef sai1_common_slot = hsai_BlockA1.SlotInit;

    memset(&hsai_BlockB1, 0, sizeof(hsai_BlockB1));
    hsai_BlockB1.Instance = SAI1_Block_B;
    hsai_BlockB1.Init = sai1_common_init;
    hsai_BlockB1.FrameInit = sai1_common_frame;
    hsai_BlockB1.SlotInit = sai1_common_slot;
    hsai_BlockB1.Init.AudioMode = SAI_MODESLAVE_RX;
    hsai_BlockB1.Init.Synchro = SAI_SYNCHRONOUS;
    hsai_BlockB1.Init.ClockStrobing = SAI_CLOCKSTROBING_RISINGEDGE;
    hsai_BlockB1.Init.OutputDrive = SAI_OUTPUTDRIVE_DISABLE;
    hsai_BlockB1.Init.TriState = SAI_OUTPUT_RELEASED;
    if (HAL_SAI_Init(&hsai_BlockB1) != HAL_OK) {
        Error_Handler();
    }

}

static void MX_DSI_Init(void)
{
    DSI_PLLInitTypeDef pll = {0};
    DSI_VidCfgTypeDef video = {0};

    hdsi.Instance = DSI;
    hdsi.Init.AutomaticClockLaneControl = DSI_AUTO_CLK_LANE_CTRL_DISABLE;
    hdsi.Init.TXEscapeCkdiv = 4;
    hdsi.Init.NumberOfLanes = DSI_TWO_DATA_LANES;
    pll.PLLNDIV = 100;
    pll.PLLIDF = DSI_PLL_IN_DIV5;
    pll.PLLODF = DSI_PLL_OUT_DIV1;

    HAL_GPIO_WritePin(DSI_RESET_GPIO_Port, DSI_RESET_Pin, GPIO_PIN_RESET);
    HAL_Delay(10);
    HAL_GPIO_WritePin(DSI_RESET_GPIO_Port, DSI_RESET_Pin, GPIO_PIN_SET);
    HAL_Delay(10);
    if (HAL_DSI_Init(&hdsi, &pll) != HAL_OK) {
        Error_Handler();
    }

    video.VirtualChannelID = 0;
    video.ColorCoding = DSI_RGB888;
    video.VSPolarity = DSI_VSYNC_ACTIVE_HIGH;
    video.HSPolarity = DSI_HSYNC_ACTIVE_HIGH;
    video.DEPolarity = DSI_DATA_ENABLE_ACTIVE_HIGH;
    video.Mode = DSI_VID_MODE_BURST;
    video.NullPacketSize = 0xFFF;
    video.NumberOfChunks = 0;
    video.PacketSize = DISPLAY_WIDTH;
    video.HorizontalSyncActive = (LCD_HSA * DSI_LANE_BYTE_CLOCK_KHZ) / LCD_CLOCK_KHZ;
    video.HorizontalBackPorch = (LCD_HBP * DSI_LANE_BYTE_CLOCK_KHZ) / LCD_CLOCK_KHZ;
    video.HorizontalLine = ((DISPLAY_WIDTH + LCD_HSA + LCD_HBP + LCD_HFP) *
                            DSI_LANE_BYTE_CLOCK_KHZ) / LCD_CLOCK_KHZ;
    video.VerticalSyncActive = LCD_VSA;
    video.VerticalBackPorch = LCD_VBP;
    video.VerticalFrontPorch = LCD_VFP;
    video.VerticalActive = DISPLAY_HEIGHT;
    video.LPCommandEnable = DSI_LP_COMMAND_ENABLE;
    video.LPLargestPacketSize = 16;
    video.LPVACTLargestPacketSize = 0;
    video.LPHorizontalFrontPorchEnable = DSI_LP_HFP_ENABLE;
    video.LPHorizontalBackPorchEnable = DSI_LP_HBP_ENABLE;
    video.LPVerticalActiveEnable = DSI_LP_VACT_ENABLE;
    video.LPVerticalFrontPorchEnable = DSI_LP_VFP_ENABLE;
    video.LPVerticalBackPorchEnable = DSI_LP_VBP_ENABLE;
    video.LPVerticalSyncActiveEnable = DSI_LP_VSYNC_ENABLE;
    video.FrameBTAAcknowledgeEnable = DSI_FBTAA_DISABLE;
    if (HAL_DSI_ConfigVideoMode(&hdsi, &video) != HAL_OK) {
        Error_Handler();
    }
}

static void MX_USART1_UART_Init(void)
{
    GPIO_InitTypeDef gpio = {0};

    __HAL_RCC_GPIOA_CLK_ENABLE();
    __HAL_RCC_USART1_CLK_ENABLE();

    gpio.Pin = VCP_TX_Pin | VCP_RX_Pin;
    gpio.Mode = GPIO_MODE_AF_PP;
    gpio.Pull = GPIO_PULLUP;
    gpio.Speed = GPIO_SPEED_FREQ_VERY_HIGH;
    gpio.Alternate = GPIO_AF7_USART1;
    HAL_GPIO_Init(GPIOA, &gpio);

    huart1.Instance = USART1;
    huart1.Init.BaudRate = 115200;
    huart1.Init.WordLength = UART_WORDLENGTH_8B;
    huart1.Init.StopBits = UART_STOPBITS_1;
    huart1.Init.Parity = UART_PARITY_NONE;
    huart1.Init.Mode = UART_MODE_TX_RX;
    huart1.Init.HwFlowCtl = UART_HWCONTROL_NONE;
    huart1.Init.OverSampling = UART_OVERSAMPLING_16;
    huart1.Init.OneBitSampling = UART_ONE_BIT_SAMPLE_DISABLE;
    huart1.AdvancedInit.AdvFeatureInit = UART_ADVFEATURE_NO_INIT;
    if (HAL_UART_Init(&huart1) != HAL_OK) {
        Error_Handler();
    }
    g_trace_ready = 1;
}

static void MX_LTDC_Init(void)
{
    LTDC_LayerCfgTypeDef layer = {0};
    hltdc.Instance = LTDC;
    hltdc.Init.HSPolarity = LTDC_HSPOLARITY_AL;
    hltdc.Init.VSPolarity = LTDC_VSPOLARITY_AL;
    hltdc.Init.DEPolarity = LTDC_DEPOLARITY_AL;
    hltdc.Init.PCPolarity = LTDC_PCPOLARITY_IPC;
    hltdc.Init.HorizontalSync = LCD_HSA - 1U;
    hltdc.Init.VerticalSync = LCD_VSA - 1U;
    hltdc.Init.AccumulatedHBP = LCD_HSA + LCD_HBP - 1U;
    hltdc.Init.AccumulatedVBP = LCD_VSA + LCD_VBP - 1U;
    hltdc.Init.AccumulatedActiveW = DISPLAY_WIDTH + LCD_HSA + LCD_HBP - 1U;
    hltdc.Init.AccumulatedActiveH = DISPLAY_HEIGHT + LCD_VSA + LCD_VBP - 1U;
    hltdc.Init.TotalWidth = DISPLAY_WIDTH + LCD_HSA + LCD_HBP + LCD_HFP - 1U;
    hltdc.Init.TotalHeigh = DISPLAY_HEIGHT + LCD_VSA + LCD_VBP + LCD_VFP - 1U;
    hltdc.Init.Backcolor.Blue = 0;
    hltdc.Init.Backcolor.Green = 0;
    hltdc.Init.Backcolor.Red = 0;
    if (HAL_LTDC_Init(&hltdc) != HAL_OK) {
        Error_Handler();
    }
    layer.WindowX0 = 0;
    layer.WindowX1 = DISPLAY_WIDTH;
    layer.WindowY0 = 0;
    layer.WindowY1 = DISPLAY_HEIGHT;
    layer.PixelFormat = LTDC_PIXEL_FORMAT_RGB565;
    layer.FBStartAdress = DISPLAY_FRAMEBUFFER;
    layer.Alpha = 255;
    layer.BlendingFactor1 = LTDC_BLENDING_FACTOR1_CA;
    layer.BlendingFactor2 = LTDC_BLENDING_FACTOR2_CA;
    layer.ImageWidth = DISPLAY_WIDTH;
    layer.ImageHeight = DISPLAY_HEIGHT;
    if (HAL_LTDC_ConfigLayer(&hltdc, &layer, 0) != HAL_OK) {
        Error_Handler();
    }
    if (HAL_DSI_Start(&hdsi) != HAL_OK) {
        Error_Handler();
    }
}

static void MX_DMA2D_Init(void)
{
    hdma2d.Instance = DMA2D;
    hdma2d.Init.Mode = DMA2D_R2M;
    hdma2d.Init.ColorMode = DMA2D_OUTPUT_RGB565;
    hdma2d.Init.OutputOffset = 0;
    HAL_DMA2D_Init(&hdma2d);
}

static void MX_FMC_Init(void)
{
    FMC_SDRAM_TimingTypeDef timing = {0};
    hsdram1.Instance = FMC_SDRAM_DEVICE;
    hsdram1.Init.SDBank = FMC_SDRAM_BANK1;
    hsdram1.Init.ColumnBitsNumber = FMC_SDRAM_COLUMN_BITS_NUM_8;
    hsdram1.Init.RowBitsNumber = FMC_SDRAM_ROW_BITS_NUM_12;
    hsdram1.Init.MemoryDataWidth = FMC_SDRAM_MEM_BUS_WIDTH_32;
    hsdram1.Init.InternalBankNumber = FMC_SDRAM_INTERN_BANKS_NUM_4;
    hsdram1.Init.CASLatency = FMC_SDRAM_CAS_LATENCY_3;
    hsdram1.Init.WriteProtection = FMC_SDRAM_WRITE_PROTECTION_DISABLE;
    hsdram1.Init.SDClockPeriod = FMC_SDRAM_CLOCK_PERIOD_2;
    hsdram1.Init.ReadBurst = FMC_SDRAM_RBURST_ENABLE;
    hsdram1.Init.ReadPipeDelay = FMC_SDRAM_RPIPE_DELAY_0;
    timing.LoadToActiveDelay = 2;
    timing.ExitSelfRefreshDelay = 7;
    timing.SelfRefreshTime = 4;
    timing.RowCycleDelay = 7;
    timing.WriteRecoveryTime = 3;
    timing.RPDelay = 2;
    timing.RCDDelay = 2;
    if (HAL_SDRAM_Init(&hsdram1, &timing) != HAL_OK) {
        Error_Handler();
    }
}

static void SDRAM_Startup(void)
{
    FMC_SDRAM_CommandTypeDef cmd = {0};
    cmd.CommandTarget = FMC_SDRAM_CMD_TARGET_BANK1;
    cmd.CommandMode = FMC_SDRAM_CMD_CLK_ENABLE;
    cmd.AutoRefreshNumber = 1;
    HAL_SDRAM_SendCommand(&hsdram1, &cmd, HAL_MAX_DELAY);
    HAL_Delay(1);
    cmd.CommandMode = FMC_SDRAM_CMD_PALL;
    HAL_SDRAM_SendCommand(&hsdram1, &cmd, HAL_MAX_DELAY);
    cmd.CommandMode = FMC_SDRAM_CMD_AUTOREFRESH_MODE;
    cmd.AutoRefreshNumber = 8;
    HAL_SDRAM_SendCommand(&hsdram1, &cmd, HAL_MAX_DELAY);
    cmd.CommandMode = FMC_SDRAM_CMD_LOAD_MODE;
    cmd.ModeRegisterDefinition = 0x0231;
    HAL_SDRAM_SendCommand(&hsdram1, &cmd, HAL_MAX_DELAY);
    HAL_SDRAM_ProgramRefreshRate(&hsdram1, 1386);
}

static void MX_GPIO_Init(void)
{
    GPIO_InitTypeDef gpio = {0};
    __HAL_RCC_GPIOA_CLK_ENABLE();
    __HAL_RCC_GPIOB_CLK_ENABLE();
    __HAL_RCC_GPIOC_CLK_ENABLE();
    __HAL_RCC_GPIOD_CLK_ENABLE();
    __HAL_RCC_GPIOE_CLK_ENABLE();
    __HAL_RCC_GPIOF_CLK_ENABLE();
    __HAL_RCC_GPIOG_CLK_ENABLE();
    __HAL_RCC_GPIOH_CLK_ENABLE();
    __HAL_RCC_GPIOI_CLK_ENABLE();
    __HAL_RCC_GPIOJ_CLK_ENABLE();

    HAL_GPIO_WritePin(GPIOJ, LD_USER1_Pin | LD_USER2_Pin | DSI_RESET_Pin, GPIO_PIN_RESET);
    gpio.Pin = LD_USER1_Pin | LD_USER2_Pin | DSI_RESET_Pin;
    gpio.Mode = GPIO_MODE_OUTPUT_PP;
    gpio.Pull = GPIO_NOPULL;
    gpio.Speed = GPIO_SPEED_FREQ_LOW;
    HAL_GPIO_Init(GPIOJ, &gpio);

    HAL_GPIO_WritePin(LCD_BL_CTRL_GPIO_Port, LCD_BL_CTRL_Pin, GPIO_PIN_SET);
    gpio.Pin = LCD_BL_CTRL_Pin;
    gpio.Mode = GPIO_MODE_OUTPUT_PP;
    HAL_GPIO_Init(LCD_BL_CTRL_GPIO_Port, &gpio);
}

static void MPU_Config(void)
{
    MPU_Region_InitTypeDef mpu = {0};
    HAL_MPU_Disable();
    mpu.Enable = MPU_REGION_ENABLE;
    mpu.Number = MPU_REGION_NUMBER0;
    mpu.BaseAddress = 0xC0000000;
    mpu.Size = MPU_REGION_SIZE_16MB;
    mpu.AccessPermission = MPU_REGION_FULL_ACCESS;
    mpu.IsBufferable = MPU_ACCESS_NOT_BUFFERABLE;
    mpu.IsCacheable = MPU_ACCESS_NOT_CACHEABLE;
    mpu.IsShareable = MPU_ACCESS_NOT_SHAREABLE;
    mpu.TypeExtField = MPU_TEX_LEVEL0;
    mpu.DisableExec = MPU_INSTRUCTION_ACCESS_ENABLE;
    HAL_MPU_ConfigRegion(&mpu);
    HAL_MPU_Enable(MPU_PRIVILEGED_DEFAULT);
}

void HAL_TIM_PeriodElapsedCallback(TIM_HandleTypeDef *htim)
{
    if (htim->Instance == TIM6) {
        HAL_IncTick();
    }
}

void Error_Handler(void)
{
    if (g_trace_ready) {
        printf("[fatal] Error_Handler\r\n");
    }
    __disable_irq();
    while (1) {
    }
}

int __io_putchar(int ch)
{
    uint8_t c = (uint8_t)ch;
    if (!g_trace_ready) {
        return ch;
    }
    if (ch == '\n') {
        uint8_t cr = '\r';
        HAL_UART_Transmit(&huart1, &cr, 1, 10);
    }
    HAL_UART_Transmit(&huart1, &c, 1, 10);
    return ch;
}

#ifdef USE_FULL_ASSERT
void assert_failed(uint8_t *file, uint32_t line)
{
    (void)file;
    (void)line;
    Error_Handler();
}
#endif
