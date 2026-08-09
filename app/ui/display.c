#include "display.h"

#include <stdio.h>
#include <string.h>

static volatile uint16_t *const fb = (volatile uint16_t *)DISPLAY_FRAMEBUFFER;

#define C_BG     0x0841U
#define C_PANEL  0x18E3U
#define C_GRID   0x39E7U
#define C_TEXT   0xFFFFU
#define C_ACCENT 0x07FFU
#define C_BAR    0xFCA0U

typedef struct {
    char c;
    uint8_t col[5];
} glyph_t;

static const glyph_t glyphs[] = {
    {' ', {0,0,0,0,0}}, {'-', {8,8,8,8,8}}, {'.', {0,0,0,0,64}}, {':', {0,0,20,0,0}},
    {'/', {64,32,16,8,4}}, {'>', {0,34,20,8,0}}, {'0', {62,81,73,69,62}},
    {'1', {0,66,127,64,0}}, {'2', {98,81,73,73,70}}, {'3', {34,65,73,73,54}},
    {'4', {24,20,18,127,16}}, {'5', {39,69,69,69,57}}, {'6', {60,74,73,73,48}},
    {'7', {1,113,9,5,3}}, {'8', {54,73,73,73,54}}, {'9', {6,73,73,41,30}},
    {'A', {126,9,9,9,126}}, {'B', {127,73,73,73,54}}, {'C', {62,65,65,65,34}},
    {'D', {127,65,65,34,28}}, {'E', {127,73,73,73,65}}, {'F', {127,9,9,9,1}},
    {'G', {62,65,73,73,122}}, {'H', {127,8,8,8,127}}, {'I', {0,65,127,65,0}},
    {'J', {32,64,65,63,1}}, {'K', {127,8,20,34,65}}, {'L', {127,64,64,64,64}},
    {'M', {127,2,12,2,127}}, {'N', {127,4,8,16,127}}, {'O', {62,65,65,65,62}},
    {'P', {127,9,9,9,6}}, {'Q', {62,65,81,33,94}}, {'R', {127,9,25,41,70}},
    {'S', {70,73,73,73,49}}, {'T', {1,1,127,1,1}}, {'U', {63,64,64,64,63}},
    {'V', {31,32,64,32,31}}, {'W', {127,32,24,32,127}}, {'X', {99,20,8,20,99}},
    {'Y', {7,8,112,8,7}}, {'Z', {97,81,73,69,67}},
};

static void put_px(int x, int y, uint16_t color)
{
    if ((unsigned)x < DISPLAY_WIDTH && (unsigned)y < DISPLAY_HEIGHT) {
        fb[(uint32_t)y * DISPLAY_WIDTH + (uint32_t)x] = color;
    }
}

static void fill_rect(int x, int y, int w, int h, uint16_t color)
{
    for (int yy = y; yy < y + h; ++yy) {
        for (int xx = x; xx < x + w; ++xx) {
            put_px(xx, yy, color);
        }
    }
}

static const uint8_t *glyph_for(char c)
{
    if (c >= 'a' && c <= 'z') {
        c = (char)(c - 'a' + 'A');
    }
    for (size_t i = 0; i < sizeof(glyphs) / sizeof(glyphs[0]); ++i) {
        if (glyphs[i].c == c) {
            return glyphs[i].col;
        }
    }
    return glyphs[0].col;
}

static void draw_char(int x, int y, char c, uint16_t color, int scale)
{
    const uint8_t *g = glyph_for(c);
    for (int col = 0; col < 5; ++col) {
        for (int row = 0; row < 7; ++row) {
            if (g[col] & (1U << row)) {
                fill_rect(x + col * scale, y + row * scale, scale, scale, color);
            }
        }
    }
}

static void draw_text(int x, int y, const char *text, uint16_t color, int scale, int max_w)
{
    int cx = x;
    int cy = y;
    const int step = 6 * scale;
    while (*text && cy < (int)DISPLAY_HEIGHT - 8 * scale) {
        if (*text == '\n' || cx + step > x + max_w) {
            cx = x;
            cy += 9 * scale;
            if (*text == '\n') {
                ++text;
            }
            continue;
        }
        draw_char(cx, cy, *text++, color, scale);
        cx += step;
    }
}

void display_init(LTDC_HandleTypeDef *hltdc)
{
    (void)hltdc;
    fill_rect(0, 0, DISPLAY_WIDTH, DISPLAY_HEIGHT, C_BG);
}

void display_status(const char *line1, const char *line2)
{
    fill_rect(0, 0, DISPLAY_WIDTH, DISPLAY_HEIGHT, C_BG);
    draw_text(24, 24, line1, C_TEXT, 3, 740);
    draw_text(24, 64, line2, C_ACCENT, 2, 740);
}

void display_render(const spectrum_t *spectrum, const char *mode, int tone_hz, const char *decoded)
{
    char header[64];
    snprintf(header, sizeof(header), "%s  TONE %d HZ  PEAK %d HZ", mode, tone_hz, spectrum->peak_hz);

    fill_rect(0, 0, DISPLAY_WIDTH, DISPLAY_HEIGHT, C_BG);
    draw_text(16, 12, header, C_TEXT, 2, 760);

    fill_rect(16, 58, 768, 210, C_PANEL);
    for (int y = 0; y <= 4; ++y) {
        fill_rect(16, 58 + y * 52, 768, 1, C_GRID);
    }
    for (int i = 0; i < SPECTRUM_BINS; ++i) {
        int h = (spectrum->bins[i] * 190) / 255;
        int x = 20 + i * 12;
        fill_rect(x, 256 - h, 8, h, (i == spectrum->peak_bin) ? C_ACCENT : C_BAR);
    }

    fill_rect(16, 288, 768, 176, C_PANEL);
    draw_text(24, 300, decoded && decoded[0] ? decoded : "WAITING FOR PSK SIGNAL", C_TEXT, 2, 740);
}
