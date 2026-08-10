#include "display.h"

#include <stdio.h>
#include <string.h>

#include "main.h"
#include "lvgl.h"
#include "otm8009a.h"

#define C_BG      0x101418U
#define C_PANEL   0x1D252CU
#define C_TEXT    0xF1F5F9U
#define C_MUTED   0x94A3B8U
#define C_ACCENT  0x22D3EEU
#define C_WARN    0xF59E0BU

#define LCD_DSI_ID 0U
#define LCD_CLOCK_KHZ 27429U
#define DSI_LANE_BYTE_CLOCK_KHZ 62500U

#define HSA 1U
#define HBP 15U
#define HFP 16U
#define VSA 2U
#define VBP 34U
#define VFP 34U

#define WATERFALL_WIDTH 764U
#define WATERFALL_HEIGHT 166U

static DSI_HandleTypeDef *g_hdsi;
static lv_disp_draw_buf_t g_draw_buf;
static lv_color_t g_line_buf[DISPLAY_WIDTH * 40U];

static lv_obj_t *g_screen;
static lv_obj_t *g_title;
static lv_obj_t *g_subtitle;
static lv_obj_t *g_mode;
static lv_obj_t *g_tone;
static lv_obj_t *g_peak;
static lv_obj_t *g_decoded;
static lv_obj_t *g_waterfall;
static lv_color_t *g_waterfall_buf = (lv_color_t *)(DISPLAY_FRAMEBUFFER + (DISPLAY_WIDTH * DISPLAY_HEIGHT * 2U));
static uint8_t g_waterfall_line[WATERFALL_WIDTH];

static void lv_flush(lv_disp_drv_t *disp_drv, const lv_area_t *area, lv_color_t *color_p)
{
    uint16_t *fb = (uint16_t *)DISPLAY_FRAMEBUFFER;
    int32_t width = area->x2 - area->x1 + 1;

    for (int32_t y = area->y1; y <= area->y2; ++y) {
        uint16_t *dst = fb + ((uint32_t)y * DISPLAY_WIDTH) + (uint32_t)area->x1;
        memcpy(dst, color_p, (size_t)width * sizeof(uint16_t));
        color_p += width;
    }

    lv_disp_flush_ready(disp_drv);
}

static lv_obj_t *make_label(lv_obj_t *parent, const lv_font_t *font, lv_color_t color)
{
    lv_obj_t *label = lv_label_create(parent);
    lv_obj_set_style_text_font(label, font, 0);
    lv_obj_set_style_text_color(label, color, 0);
    lv_obj_set_style_bg_opa(label, LV_OPA_TRANSP, 0);
    return label;
}

static lv_obj_t *make_panel(lv_obj_t *parent, lv_coord_t x, lv_coord_t y, lv_coord_t w, lv_coord_t h)
{
    lv_obj_t *panel = lv_obj_create(parent);
    lv_obj_set_pos(panel, x, y);
    lv_obj_set_size(panel, w, h);
    lv_obj_set_style_bg_color(panel, lv_color_hex(C_PANEL), 0);
    lv_obj_set_style_bg_opa(panel, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(panel, 0, 0);
    lv_obj_set_style_radius(panel, 4, 0);
    lv_obj_set_style_pad_all(panel, 0, 0);
    lv_obj_clear_flag(panel, LV_OBJ_FLAG_SCROLLABLE);
    return panel;
}

static uint32_t lerp_u8(uint32_t a, uint32_t b, uint32_t t)
{
    return ((a * (255U - t)) + (b * t)) / 255U;
}

static lv_color_t waterfall_color(uint8_t level)
{
    static const uint32_t stops[] = {
        0x070A0FU,
        0x10223DU,
        0x124C6FU,
        0x1E8A5AU,
        0xD6B13DU,
        0xF97316U,
    };
    const uint32_t segments = (uint32_t)(sizeof(stops) / sizeof(stops[0]) - 1U);
    const uint32_t scaled = (uint32_t)level * segments;
    uint32_t idx = scaled / 255U;
    uint32_t frac = scaled % 255U;
    if (idx >= segments) {
        idx = segments - 1U;
        frac = 255U;
    }

    const uint32_t a = stops[idx];
    const uint32_t b = stops[idx + 1U];
    const uint32_t r = lerp_u8((a >> 16) & 0xffU, (b >> 16) & 0xffU, frac);
    const uint32_t g = lerp_u8((a >> 8) & 0xffU, (b >> 8) & 0xffU, frac);
    const uint32_t bl = lerp_u8(a & 0xffU, b & 0xffU, frac);
    return lv_color_hex((r << 16) | (g << 8) | bl);
}

static uint8_t interp_bin(const spectrum_t *spectrum, uint32_t x)
{
    const uint32_t scaled = (x * (SPECTRUM_BINS - 1U) * 256U) / (WATERFALL_WIDTH - 1U);
    uint32_t bin = scaled / 256U;
    uint32_t frac = scaled & 0xffU;
    if (bin >= SPECTRUM_BINS - 1U) {
        return spectrum->bins[SPECTRUM_BINS - 1U];
    }
    uint32_t a = spectrum->bins[bin];
    uint32_t b = spectrum->bins[bin + 1U];
    return (uint8_t)(((a * (256U - frac)) + (b * frac)) / 256U);
}

static void waterfall_push(const spectrum_t *spectrum)
{
    memmove(&g_waterfall_buf[WATERFALL_WIDTH],
            &g_waterfall_buf[0],
            (WATERFALL_HEIGHT - 1U) * WATERFALL_WIDTH * sizeof(g_waterfall_buf[0]));

    for (uint32_t x = 0; x < WATERFALL_WIDTH; ++x) {
        uint32_t level = interp_bin(spectrum, x);
        level = ((uint32_t)g_waterfall_line[x] * 3U + level * 2U) / 5U;
        g_waterfall_line[x] = (uint8_t)level;
        g_waterfall_buf[x] = waterfall_color((uint8_t)level);
    }

    lv_obj_invalidate(g_waterfall);
}

static void create_ui(void)
{
    g_screen = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(g_screen, lv_color_hex(C_BG), 0);
    lv_obj_set_style_bg_opa(g_screen, LV_OPA_COVER, 0);
    lv_obj_clear_flag(g_screen, LV_OBJ_FLAG_SCROLLABLE);

    g_title = make_label(g_screen, &lv_font_montserrat_24, lv_color_hex(C_TEXT));
    lv_obj_set_pos(g_title, 18, 12);
    lv_label_set_text(g_title, "STM32F769I PSK Modem");

    g_subtitle = make_label(g_screen, &lv_font_montserrat_14, lv_color_hex(C_MUTED));
    lv_obj_set_pos(g_subtitle, 20, 44);
    lv_label_set_text(g_subtitle, "LVGL display active");

    lv_obj_t *status = make_panel(g_screen, 18, 72, 764, 54);
    g_mode = make_label(status, &lv_font_montserrat_20, lv_color_hex(C_ACCENT));
    lv_obj_set_pos(g_mode, 18, 15);
    g_tone = make_label(status, &lv_font_montserrat_16, lv_color_hex(C_TEXT));
    lv_obj_set_pos(g_tone, 170, 18);
    g_peak = make_label(status, &lv_font_montserrat_16, lv_color_hex(C_TEXT));
    lv_obj_set_pos(g_peak, 360, 18);

    lv_obj_t *spectrum_panel = make_panel(g_screen, 18, 144, 764, 166);
    g_waterfall = lv_canvas_create(spectrum_panel);
    lv_obj_set_pos(g_waterfall, 0, 0);
    lv_canvas_set_buffer(g_waterfall, g_waterfall_buf,
                         WATERFALL_WIDTH, WATERFALL_HEIGHT, LV_IMG_CF_TRUE_COLOR);
    lv_canvas_fill_bg(g_waterfall, lv_color_hex(0x111827U), LV_OPA_COVER);

    lv_obj_t *text_panel = make_panel(g_screen, 18, 328, 764, 134);
    g_decoded = make_label(text_panel, &lv_font_montserrat_16, lv_color_hex(C_TEXT));
    lv_obj_set_pos(g_decoded, 16, 14);
    lv_obj_set_size(g_decoded, 732, 108);
    lv_label_set_long_mode(g_decoded, LV_LABEL_LONG_WRAP);
    lv_label_set_text(g_decoded, "Waiting for PSK signal");

    lv_scr_load(g_screen);
}

static void panel_init(void)
{
    printf("[display] otm8009a init begin\r\n");
    OTM8009A_Init(OTM8009A_FORMAT_RGB888, OTM8009A_ORIENTATION_LANDSCAPE);
    printf("[display] otm8009a init done\r\n");
    HAL_DSI_ShortWrite(g_hdsi, LCD_DSI_ID, DSI_DCS_SHORT_PKT_WRITE_P1,
                       OTM8009A_CMD_DISPON, 0x00);
    printf("[display] display on command sent\r\n");
    HAL_GPIO_WritePin(LCD_BL_CTRL_GPIO_Port, LCD_BL_CTRL_Pin, GPIO_PIN_SET);
    printf("[display] backlight enabled\r\n");
}

void display_init(LTDC_HandleTypeDef *hltdc, DSI_HandleTypeDef *hdsi)
{
    (void)hltdc;
    g_hdsi = hdsi;

    printf("[display] init begin\r\n");
    panel_init();

    printf("[display] lv_init begin\r\n");
    lv_init();
    lv_disp_draw_buf_init(&g_draw_buf, g_line_buf, NULL,
                          DISPLAY_WIDTH * 40U);

    static lv_disp_drv_t disp_drv;
    lv_disp_drv_init(&disp_drv);
    disp_drv.hor_res = DISPLAY_WIDTH;
    disp_drv.ver_res = DISPLAY_HEIGHT;
    disp_drv.flush_cb = lv_flush;
    disp_drv.draw_buf = &g_draw_buf;
    lv_disp_drv_register(&disp_drv);

    create_ui();
    printf("[display] lvgl ui created\r\n");
    lv_refr_now(NULL);
    printf("[display] first refresh done\r\n");
}

void display_task(void)
{
    lv_timer_handler();
}

void display_status(const char *line1, const char *line2)
{
    lv_label_set_text(g_title, line1 ? line1 : "");
    lv_label_set_text(g_subtitle, line2 ? line2 : "");
    lv_label_set_text(g_mode, "STATUS");
    lv_label_set_text(g_tone, "");
    lv_label_set_text(g_peak, "");
    lv_label_set_text(g_decoded, line2 ? line2 : "");
    lv_refr_now(NULL);
}

void display_render(const spectrum_t *spectrum, const char *mode, int tone_hz, const char *decoded)
{
    char text[64];

    lv_label_set_text(g_title, "STM32F769I PSK Modem");
    lv_label_set_text(g_subtitle, "Line IN -> WM8994 -> SAI1B DMA");
    lv_label_set_text(g_mode, mode ? mode : "PSK");

    snprintf(text, sizeof(text), "Tone %d Hz", tone_hz);
    lv_label_set_text(g_tone, text);

    snprintf(text, sizeof(text), "Peak %d Hz", spectrum->peak_hz);
    lv_label_set_text(g_peak, text);

    waterfall_push(spectrum);

    lv_label_set_text(g_decoded, decoded && decoded[0] ? decoded : "Waiting for PSK signal");
    display_task();
}

void DSI_IO_WriteCmd(uint32_t nbr_params, uint8_t *params)
{
    printf("[display] dsi cmd 0x%02x len %lu\r\n", params[0], nbr_params);
    if (nbr_params <= 1U) {
        HAL_DSI_ShortWrite(g_hdsi, LCD_DSI_ID, DSI_DCS_SHORT_PKT_WRITE_P1,
                           params[0], params[1]);
    } else {
        HAL_DSI_LongWrite(g_hdsi, LCD_DSI_ID, DSI_DCS_LONG_PKT_WRITE,
                          nbr_params, params[nbr_params], params);
    }
}

int32_t DSI_IO_ReadCmd(uint32_t reg, uint8_t *data, uint32_t size)
{
    return HAL_DSI_Read(g_hdsi, LCD_DSI_ID, data, size,
                        DSI_DCS_SHORT_PKT_READ, reg, data) == HAL_OK ? 0 : -1;
}

void OTM8009A_IO_Delay(uint32_t delay)
{
    HAL_Delay(delay);
}
