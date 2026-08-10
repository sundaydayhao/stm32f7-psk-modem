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
static lv_obj_t *g_bars[SPECTRUM_BINS];

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
    const lv_coord_t bar_w = 8;
    const lv_coord_t gap = 4;
    const lv_coord_t x0 = 15;
    for (int i = 0; i < SPECTRUM_BINS; ++i) {
        lv_obj_t *bar = lv_bar_create(spectrum_panel);
        lv_obj_set_size(bar, bar_w, 132);
        lv_obj_set_pos(bar, x0 + i * (bar_w + gap), 17);
        lv_bar_set_range(bar, 0, 255);
        lv_bar_set_value(bar, 0, LV_ANIM_OFF);
        lv_obj_set_style_bg_color(bar, lv_color_hex(0x26313AU), LV_PART_MAIN);
        lv_obj_set_style_bg_opa(bar, LV_OPA_COVER, LV_PART_MAIN);
        lv_obj_set_style_radius(bar, 2, LV_PART_MAIN);
        lv_obj_set_style_bg_color(bar, lv_color_hex(C_WARN), LV_PART_INDICATOR);
        lv_obj_set_style_bg_opa(bar, LV_OPA_COVER, LV_PART_INDICATOR);
        lv_obj_set_style_radius(bar, 2, LV_PART_INDICATOR);
        g_bars[i] = bar;
    }

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

    for (int i = 0; i < SPECTRUM_BINS; ++i) {
        lv_bar_set_value(g_bars[i], spectrum->bins[i], LV_ANIM_OFF);
        lv_obj_set_style_bg_color(g_bars[i],
                                  lv_color_hex(i == spectrum->peak_bin ? C_ACCENT : C_WARN),
                                  LV_PART_INDICATOR);
    }

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
