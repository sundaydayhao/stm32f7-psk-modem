#include "psk_rtty.h"

#include <math.h>
#include <string.h>

#include "PSKTables.h"
#include "pskcore_rx_tables.h"

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

#define TWO_PI 6.28318530717958647692f
#define PSKCORE_TS 0.032f
#define PSKCORE_AFC_OFF 0
#define PSKCORE_AFC_ON 1
#define PSKCORE_AFC_TIMELIMIT 10
#define PSKCORE_NLP_K 100.0f
#define PHZ_180_BMIN 0.0f
#define PHZ_180_BMAX ((float)M_PI)
#define PHZ_0_BMIN ((float)M_PI)
#define PHZ_0_BMAX TWO_PI
#define PHZ_ERR_SIGN_LIMIT 5
#define SQ_EMIT_STABLE_SYMBOLS 10

static unsigned char s_varicode_decode[2048];
static int s_tables_ready;

static const unsigned char s_rtty_table[0x20][2] = {
    {0, 0}, {'E', '3'}, {'\n', '\n'}, {'A', '-'}, {' ', ' '}, {'S', '\''}, {'I', '8'}, {'U', '7'},
    {'\r', '\r'}, {'D', '$'}, {'R', '4'}, {'J', 7}, {'N', ','}, {'F', '!'}, {'C', ':'}, {'K', '('},
    {'T', '5'}, {'Z', '"'}, {'L', ')'}, {'W', '2'}, {'H', '#'}, {'Y', '6'}, {'P', '0'}, {'Q', '1'},
    {'O', '9'}, {'B', '?'}, {'G', '&'}, {0, 0}, {'M', '.'}, {'X', '/'}, {0, 0}, {0, 0},
};

static void init_tables(void)
{
    if (s_tables_ready) {
        return;
    }
    memset(s_varicode_decode, 0, sizeof(s_varicode_decode));
    for (int ch = 0; ch < 256; ++ch) {
        const unsigned int code = VARICODE_TABLE[ch];
        if (code < sizeof(s_varicode_decode)) {
            s_varicode_decode[code] = (unsigned char)ch;
        }
    }
    s_tables_ready = 1;
}

static float symbol_rate_for_mode(psk_rtty_mode_t mode)
{
    switch (mode) {
    case PSK_RTTY_MODE_PSK63:
        return 62.5f;
    case PSK_RTTY_MODE_RTTY:
        return 45.45f;
    case PSK_RTTY_MODE_BPSK31:
    case PSK_RTTY_MODE_QPSK31_USB:
    case PSK_RTTY_MODE_QPSK31_LSB:
    default:
        return 31.25f;
    }
}

static void reset_runtime(psk_rtty_rx_t *rx)
{
    rx->nco_phase = 0.0f;
    rx->nco_phase_inc = TWO_PI * (float)rx->config.rx_tone_hz / (float)rx->config.sample_rate_hz;
    rx->nco_cos = 1.0f;
    rx->nco_sin = 0.0f;
    rx->freq_error = 0.0f;
    rx->afc_limit = 50.0f * TWO_PI / (float)rx->config.sample_rate_hz;
    rx->afc_min = rx->nco_phase_inc - rx->afc_limit;
    rx->afc_max = rx->nco_phase_inc + rx->afc_limit;
    if (rx->afc_min < 0.0f) {
        rx->afc_min = 0.0f;
    }
    rx->fferr_ave = 0.0f;
    rx->fperr_ave = 0.0f;
    rx->q_freq_error = 0.0f;
    rx->nlp_k = PSKCORE_NLP_K;
    rx->agc_ave = 0.0f;
    rx->dev_ave = 90.0f;
    rx->bit_phase_inc = 16.0f / (float)rx->config.sample_rate_hz;
    rx->bit_phase_pos = 0.0f;
    rx->prev_symbol_phase = 0.0f;
    rx->symbol_clock = 0.0f;
    rx->symbol_period_samples = (float)rx->config.sample_rate_hz / symbol_rate_for_mode(rx->config.mode);
    rx->i_acc = 0.0f;
    rx->q_acc = 0.0f;
    rx->samples_in_symbol = 0;
    rx->sample_count = 0;
    rx->dec1_state = PSKCORE_DEC4_LPFIR_LENGTH - 1;
    rx->dec2_state = PSKCORE_DEC4_LPFIR_LENGTH - 1;
    rx->bit_fir_state = PSKCORE_BITFIR_LENGTH - 1;
    rx->pk_pos = 0;
    rx->new_pk_pos = 5;
    rx->raw_pk_pos = 5;
    rx->bit_pos = 0;
    rx->warmup_symbols = rx->config.mode == PSK_RTTY_MODE_PSK63 ? 64 : 32;
    rx->last_pk_pos = 0;
    rx->sq_open = 0;
    rx->sq_was_open = 0;
    rx->sq_stable_symbols = 0;
    rx->sq_level = 10;
    rx->sq_thresh = 35;
    rx->squelch_speed = 75;
    rx->on_count = 0;
    rx->off_count = 0;
    rx->p_count = 0;
    rx->n_count = 0;
    rx->bit_acc = 0;
    rx->last_bit_zero = 0;
    rx->rtty_figures = 0;
    rx->rtty_bit_index = 0;
    rx->rtty_shift_reg = 0;
    rx->i0 = 0.0f;
    rx->i1 = 0.0f;
    rx->q0 = 0.0f;
    rx->q1 = 0.0f;
    memset(rx->sync_ave, 0, sizeof(rx->sync_ave));
    memset(rx->dec1_queue, 0, sizeof(rx->dec1_queue));
    memset(rx->dec2_queue, 0, sizeof(rx->dec2_queue));
    memset(rx->bit_queue, 0, sizeof(rx->bit_queue));
    memset(&rx->freq_signal, 0, sizeof(rx->freq_signal));
    memset(&rx->bit_signal, 0, sizeof(rx->bit_signal));
    memset(&rx->z1, 0, sizeof(rx->z1));
    memset(&rx->z2, 0, sizeof(rx->z2));
}

void psk_rtty_rx_init(psk_rtty_rx_t *rx, const psk_rtty_config_t *config)
{
    init_tables();
    memset(rx, 0, sizeof(*rx));
    rx->config = *config;
    reset_runtime(rx);
}

void psk_rtty_rx_set_mode(psk_rtty_rx_t *rx, psk_rtty_mode_t mode)
{
    if (mode >= PSK_RTTY_MODE_COUNT) {
        return;
    }
    rx->config.mode = mode;
    reset_runtime(rx);
}

void psk_rtty_rx_set_tone(psk_rtty_rx_t *rx, int tone_hz)
{
    if (tone_hz < 100) {
        tone_hz = 100;
    } else if (tone_hz > 3500) {
        tone_hz = 3500;
    }
    rx->config.rx_tone_hz = tone_hz;
    reset_runtime(rx);
}

static void emit(psk_rtty_rx_t *rx, char c)
{
    if (c && rx->config.char_cb) {
        rx->config.char_cb(c, rx->config.userdata);
    }
}

static void push_varicode_bit(psk_rtty_rx_t *rx, int bit)
{
    if (bit == 0) {
        if (rx->last_bit_zero) {
            rx->debug_delims++;
            if (rx->sq_open && rx->sq_stable_symbols >= SQ_EMIT_STABLE_SYMBOLS && rx->bit_acc < 2048) {
                const unsigned char ch = s_varicode_decode[rx->bit_acc];
                if (ch != 0) {
                    rx->debug_chars++;
                    emit(rx, (char)ch);
                } else if (rx->bit_acc != 0) {
                    rx->debug_unknown++;
                }
            }
            rx->bit_acc = 0;
            rx->last_bit_zero = 0;
        } else {
            rx->last_bit_zero = 1;
        }
        return;
    }

    if (rx->last_bit_zero) {
        rx->bit_acc <<= 1;
        rx->last_bit_zero = 0;
    }
    rx->bit_acc = (uint16_t)(((rx->bit_acc << 1) | 1U) & 0x07ffU);
}

static void push_rtty_bit(psk_rtty_rx_t *rx, int bit)
{
    if (rx->rtty_bit_index == 0) {
        if (bit == 0) {
            rx->rtty_shift_reg = 0;
            rx->rtty_bit_index = 1;
        }
        return;
    }
    if (rx->rtty_bit_index <= 5) {
        rx->rtty_shift_reg >>= 1;
        if (bit) {
            rx->rtty_shift_reg |= 0x10;
        }
        rx->rtty_bit_index++;
        return;
    }

    if (bit) {
        const uint8_t code = rx->rtty_shift_reg & 0x1f;
        if (code == 0x1b) {
            rx->rtty_figures = 1;
        } else if (code == 0x1f) {
            rx->rtty_figures = 0;
        } else {
            emit(rx, (char)s_rtty_table[code][rx->rtty_figures]);
        }
    }
    rx->rtty_bit_index = 0;
}

static void finish_symbol(psk_rtty_rx_t *rx)
{
    if (rx->samples_in_symbol == 0) {
        return;
    }

    const float phase = atan2f(rx->q_acc, rx->i_acc);
    float diff = phase - rx->prev_symbol_phase;
    while (diff > (float)M_PI) {
        diff -= TWO_PI;
    }
    while (diff < -(float)M_PI) {
        diff += TWO_PI;
    }
    rx->prev_symbol_phase = phase;

    if (rx->config.mode == PSK_RTTY_MODE_RTTY) {
        push_rtty_bit(rx, rx->i_acc > 0.0f ? 1 : 0);
    } else {
        const int bit = fabsf(diff) > ((float)M_PI * 0.5f) ? 0 : 1;
        push_varicode_bit(rx, bit);
    }

    rx->i_acc = 0.0f;
    rx->q_acc = 0.0f;
    rx->samples_in_symbol = 0;
}

static void finish_simple_bpsk_symbol(psk_rtty_rx_t *rx)
{
    if (rx->samples_in_symbol == 0) {
        return;
    }

    const float i = rx->i_acc;
    const float q = rx->q_acc;
    const float mag = sqrtf(i * i + q * q) / (float)rx->samples_in_symbol;

    if (mag > rx->agc_ave) {
        rx->agc_ave = (1.0f - 1.0f / 50.0f) * rx->agc_ave + (1.0f / 50.0f) * mag;
    } else {
        rx->agc_ave = (1.0f - 1.0f / 200.0f) * rx->agc_ave + (1.0f / 200.0f) * mag;
    }
    rx->sq_level = rx->agc_ave > 50.0f ? 99 : 0;
    rx->sq_open = rx->sq_level >= rx->sq_thresh;
    if (rx->sq_open && rx->sq_stable_symbols < SQ_EMIT_STABLE_SYMBOLS) {
        rx->sq_stable_symbols++;
    }

    if (rx->debug_symbols > 0) {
        const float dot = rx->i0 * i + rx->q0 * q;
        const int bit = dot < 0.0f;

        if (rx->warmup_symbols > 0) {
            rx->warmup_symbols--;
        } else {
            rx->debug_bits++;
            if (bit) {
                rx->debug_ones++;
            } else {
                rx->debug_zeros++;
            }
            push_varicode_bit(rx, bit);
        }
    }

    rx->i0 = i;
    rx->q0 = q;
    rx->debug_symbols++;
    rx->i_acc = 0.0f;
    rx->q_acc = 0.0f;
    rx->samples_in_symbol = 0;
}

static psk_rtty_complex_t fir_complex(const psk_rtty_complex_t *queue, int newest, int length, const float *coef)
{
    psk_rtty_complex_t acc = {0.0f, 0.0f};
    for (int i = 0; i < length; ++i) {
        const int coef_idx = (length - newest + i) % length;
        acc.x += queue[i].x * coef[coef_idx];
        acc.y += queue[i].y * coef[coef_idx];
    }
    return acc;
}

static void calc_bit_filter(psk_rtty_rx_t *rx, psk_rtty_complex_t sample)
{
    rx->bit_queue[rx->bit_fir_state] = sample;
    rx->freq_signal = fir_complex(rx->bit_queue, rx->bit_fir_state, PSKCORE_BITFIR_LENGTH, s_freq_fir);
    rx->bit_signal = fir_complex(rx->bit_queue, rx->bit_fir_state, PSKCORE_BITFIR_LENGTH, s_bit_fir);
    if (--rx->bit_fir_state < 0) {
        rx->bit_fir_state = PSKCORE_BITFIR_LENGTH - 1;
    }
}

static void calc_agc(psk_rtty_rx_t *rx)
{
    const float mag = sqrtf(rx->freq_signal.x * rx->freq_signal.x + rx->freq_signal.y * rx->freq_signal.y);
    if (mag > rx->agc_ave) {
        rx->agc_ave = (1.0f - 1.0f / 200.0f) * rx->agc_ave + (1.0f / 200.0f) * mag;
    } else {
        rx->agc_ave = (1.0f - 1.0f / 500.0f) * rx->agc_ave + (1.0f / 500.0f) * mag;
    }

    if (rx->agc_ave >= 1.0f) {
        rx->bit_signal.x /= rx->agc_ave;
        rx->bit_signal.y /= rx->agc_ave;
        rx->freq_signal.x /= rx->agc_ave;
        rx->freq_signal.y /= rx->agc_ave;
    }
}

static void calc_freq_error(psk_rtty_rx_t *rx)
{
    const float p_gn = 0.001f;
    const float i_gn = 1.5e-6f;
    const float p_cgn = 0.0004f;
    const float i_cgn = 3.0e-6f;
    const float wide_gn = 1.0f / 0.02f;
    const float wlp_k = 200.0f;

    float ferr = (rx->freq_signal.x - rx->z2.x) * rx->z1.y -
                 (rx->freq_signal.y - rx->z2.y) * rx->z1.x;
    rx->z2 = rx->z1;
    rx->z1 = rx->freq_signal;

    if (ferr > 0.30f) {
        ferr = 0.30f;
    } else if (ferr < -0.30f) {
        ferr = -0.30f;
    }

    rx->fferr_ave = (1.0f - 1.0f / wlp_k) * rx->fferr_ave + (wide_gn / wlp_k) * ferr;
    if (rx->afc_timer > 0) {
        ferr = rx->fferr_ave;
        if (ferr > 0.3f || ferr < -0.3f) {
            rx->nco_phase_inc += ferr * i_cgn;
        }
        rx->freq_error = ferr * p_cgn;
    } else {
        ferr = (rx->fferr_ave * rx->fperr_ave > 0.0f) ? rx->fperr_ave : 0.0f;
        if (ferr > 0.3f || ferr < -0.3f) {
            rx->nco_phase_inc += ferr * i_gn;
        }
        rx->freq_error = ferr * p_gn;
    }

    if (rx->nco_phase_inc + rx->freq_error > rx->afc_max) {
        rx->nco_phase_inc = rx->afc_max;
        rx->freq_error = 0.0f;
    } else if (rx->nco_phase_inc + rx->freq_error < rx->afc_min) {
        rx->nco_phase_inc = rx->afc_min;
        rx->freq_error = 0.0f;
    }
}

static int symbol_sync(psk_rtty_rx_t *rx, psk_rtty_complex_t sample)
{
    int trigger = 0;
    int bit_pos = rx->bit_pos;

    if (bit_pos < 16) {
        float energy = sample.x * sample.x + sample.y * sample.y;
        if (energy > 4.0f) {
            energy = 1.0f;
        }
        rx->sync_ave[bit_pos] = (1.0f - 1.0f / 82.0f) * rx->sync_ave[bit_pos] + (1.0f / 82.0f) * energy;
        trigger = (bit_pos == rx->pk_pos);

        if (bit_pos == s_half_tbl[rx->new_pk_pos]) {
            rx->pk_pos = rx->new_pk_pos;
        }
        bit_pos++;
    }

    rx->bit_phase_pos += rx->bit_phase_inc;
    if (rx->bit_phase_pos >= PSKCORE_TS) {
        rx->bit_phase_pos = fmodf(rx->bit_phase_pos, PSKCORE_TS);
        if (bit_pos == 15 && rx->pk_pos == 15) {
            trigger = 1;
        }
        bit_pos = 0;

        float max_energy = -1.0e10f;
        int max_pos = rx->new_pk_pos;
        for (int i = 0; i < 16; ++i) {
            if (rx->sync_ave[i] > max_energy) {
                max_pos = i;
                max_energy = rx->sync_ave[i];
            }
        }
        rx->raw_pk_pos = max_pos;

        if (rx->sq_open) {
            int delta = max_pos - rx->pk_pos;
            if (delta > 8) {
                delta -= 16;
            } else if (delta < -8) {
                delta += 16;
            }
            if (delta > 1) {
                rx->new_pk_pos = (rx->pk_pos + 1) & 0x0f;
            } else if (delta < -1) {
                rx->new_pk_pos = (rx->pk_pos + 15) & 0x0f;
            } else {
                rx->new_pk_pos = max_pos;
            }
        } else {
            rx->new_pk_pos = max_pos;
        }
        rx->last_pk_pos = rx->pk_pos;
    }

    rx->bit_pos = bit_pos;
    return trigger;
}

static void calc_quality_bpsk(psk_rtty_rx_t *rx, float angle)
{
    const float phzderived_gn = 1.0f / 0.2f;
    float temp;
    const int was_open = rx->sq_open;

    if (angle >= PHZ_180_BMIN && angle <= PHZ_180_BMAX) {
        temp = angle - ((float)M_PI / 2.0f);
        rx->q_freq_error = temp;
        temp = 150.0f * fabsf(temp);
        if (temp < rx->dev_ave) {
            rx->dev_ave = (1.0f - 1.0f / rx->squelch_speed) * rx->dev_ave + (1.0f / rx->squelch_speed) * temp;
        } else {
            rx->dev_ave = (1.0f - 1.0f / (rx->squelch_speed * 2.0f)) * rx->dev_ave +
                          (1.0f / (rx->squelch_speed * 2.0f)) * temp;
        }
        if (rx->on_count > 20) {
            rx->dev_ave = 25.0f;
        } else {
            rx->on_count++;
        }
        rx->off_count = 0;
        if (rx->q_freq_error >= 0.0f) {
            rx->p_count++;
            rx->n_count = 0;
        } else {
            rx->n_count++;
            rx->p_count = 0;
        }
        if (rx->p_count < PHZ_ERR_SIGN_LIMIT && rx->n_count < PHZ_ERR_SIGN_LIMIT) {
            rx->q_freq_error = 0.0f;
        }
    } else if (angle >= PHZ_0_BMIN && angle <= PHZ_0_BMAX) {
        temp = angle - (3.0f * (float)M_PI / 2.0f);
        rx->q_freq_error = temp;
        temp = 150.0f * fabsf(temp);
        if (temp < rx->dev_ave) {
            rx->dev_ave = (1.0f - 1.0f / rx->squelch_speed) * rx->dev_ave + (1.0f / rx->squelch_speed) * temp;
        } else {
            rx->dev_ave = (1.0f - 1.0f / (rx->squelch_speed * 2.0f)) * rx->dev_ave +
                          (1.0f / (rx->squelch_speed * 2.0f)) * temp;
        }
        if (rx->off_count > 20) {
            rx->dev_ave = 100.0f;
        } else {
            rx->off_count++;
        }
        rx->on_count = 0;
        if (rx->q_freq_error >= 0.0f) {
            rx->p_count++;
            rx->n_count = 0;
        } else {
            rx->n_count++;
            rx->p_count = 0;
        }
        if (rx->p_count < PHZ_ERR_SIGN_LIMIT && rx->n_count < PHZ_ERR_SIGN_LIMIT) {
            rx->q_freq_error = 0.0f;
        }
    }

    if (rx->agc_ave > 10.0f) {
        rx->sq_level = 100 - (int)rx->dev_ave;
        rx->sq_open = rx->sq_level >= rx->sq_thresh;
    } else {
        rx->sq_level = 0;
        rx->sq_open = 0;
    }
    if (rx->sq_open) {
        if (!was_open) {
            rx->bit_acc = 0;
            rx->last_bit_zero = 0;
            rx->sq_stable_symbols = 0;
        } else if (rx->sq_stable_symbols < SQ_EMIT_STABLE_SYMBOLS) {
            rx->sq_stable_symbols++;
        }
    } else {
        rx->bit_acc = 0;
        rx->last_bit_zero = 0;
        rx->sq_stable_symbols = 0;
    }
    rx->sq_was_open = rx->sq_open;

    if (rx->q_freq_error > 1.0f) {
        rx->q_freq_error = 1.0f;
    } else if (rx->q_freq_error < -1.0f) {
        rx->q_freq_error = -1.0f;
    }
    rx->fperr_ave = (1.0f - 1.0f / rx->nlp_k) * rx->fperr_ave +
                    (phzderived_gn / rx->nlp_k) * rx->q_freq_error;
}

static void decode_bpsk_symbol(psk_rtty_rx_t *rx, psk_rtty_complex_t sample)
{
    rx->i1 = rx->i0;
    rx->q1 = rx->q0;
    rx->i0 = sample.x;
    rx->q0 = sample.y;

    psk_rtty_complex_t vect = {
        .x = rx->i1 * rx->q0 - rx->i0 * rx->q1,
        .y = rx->i1 * rx->i0 + rx->q1 * rx->q0,
    };
    float angle = (float)M_PI + atan2f(vect.y, vect.x);
    if (angle < 0.0f) {
        angle += TWO_PI;
    } else if (angle >= TWO_PI) {
        angle -= TWO_PI;
    }
    calc_quality_bpsk(rx, angle);

    const int bit = vect.y > 0.0f;
    rx->debug_symbols++;
    if (rx->warmup_symbols > 0) {
        rx->warmup_symbols--;
        return;
    }
    rx->debug_bits++;
    if (bit) {
        rx->debug_ones++;
    } else {
        rx->debug_zeros++;
    }
    push_varicode_bit(rx, bit);
}

static void __attribute__((unused)) pskcore_rx_feed(psk_rtty_rx_t *rx, const int16_t *samples, size_t count)
{
    if (rx->afc_timer > 0 && --rx->afc_timer <= 0) {
        rx->afc_timer = 0;
        rx->afc_max = rx->nco_phase_inc + rx->afc_limit;
        rx->afc_min = rx->nco_phase_inc - rx->afc_limit;
        if (rx->afc_min < 0.0f) {
            rx->afc_min = 0.0f;
        }
    }

    const int second_decim = rx->config.mode == PSK_RTTY_MODE_PSK63 ? 8 : 16;
    float rot_inc = rx->nco_phase_inc + rx->freq_error;
    float rot_cos = cosf(rot_inc);
    float rot_sin = sinf(rot_inc);
    for (size_t n = 0; n < count; ++n) {
        const float sample = (float)samples[n];
        rx->dec1_queue[rx->dec1_state].x = sample * rx->nco_cos;
        rx->dec1_queue[rx->dec1_state].y = sample * rx->nco_sin;

        const float next_cos = rx->nco_cos * rot_cos - rx->nco_sin * rot_sin;
        const float next_sin = rx->nco_sin * rot_cos + rx->nco_cos * rot_sin;
        rx->nco_cos = next_cos;
        rx->nco_sin = next_sin;
        if ((rx->sample_count & 0xff) == 0) {
            const float mag = sqrtf(rx->nco_cos * rx->nco_cos + rx->nco_sin * rx->nco_sin);
            if (mag > 0.0f) {
                rx->nco_cos /= mag;
                rx->nco_sin /= mag;
            }
        }

        if ((++rx->sample_count % 4) == 0) {
            rx->dec2_queue[rx->dec2_state] = fir_complex(rx->dec1_queue, rx->dec1_state,
                                                         PSKCORE_DEC4_LPFIR_LENGTH, s_dec4_lpfir);

            if ((rx->sample_count % second_decim) == 0) {
                psk_rtty_complex_t filtered = fir_complex(rx->dec2_queue, rx->dec2_state,
                                                        PSKCORE_DEC4_LPFIR_LENGTH, s_dec4_lpfir);
                calc_bit_filter(rx, filtered);
                calc_agc(rx);
                calc_freq_error(rx);
                rot_inc = rx->nco_phase_inc + rx->freq_error;
                rot_cos = cosf(rot_inc);
                rot_sin = sinf(rot_inc);
                if (symbol_sync(rx, rx->bit_signal)) {
                    decode_bpsk_symbol(rx, rx->bit_signal);
                }
            }

            if (--rx->dec2_state < 0) {
                rx->dec2_state = PSKCORE_DEC4_LPFIR_LENGTH - 1;
            }
        }

        if (--rx->dec1_state < 0) {
            rx->dec1_state = PSKCORE_DEC4_LPFIR_LENGTH - 1;
        }
    }
    rx->sample_count %= 16;
}

static void simple_bpsk_rx_feed(psk_rtty_rx_t *rx, const int16_t *samples, size_t count)
{
    const float phase_inc = TWO_PI * (float)rx->config.rx_tone_hz / (float)rx->config.sample_rate_hz;
    const int initial_skip = (int)(rx->symbol_period_samples * 5.0f);
    const int window_start = (int)(rx->symbol_period_samples * 0.13f);
    const int window_end = (int)(rx->symbol_period_samples * 0.87f);

    for (size_t n = 0; n < count; ++n) {
        const float s = (float)samples[n];
        const float c = cosf(rx->nco_phase);
        const float si = sinf(rx->nco_phase);

        rx->nco_phase += phase_inc;
        if (rx->nco_phase >= TWO_PI) {
            rx->nco_phase -= TWO_PI;
        }

        if (rx->sample_count < initial_skip) {
            rx->sample_count++;
            continue;
        }

        if (rx->bit_pos >= window_start && rx->bit_pos < window_end) {
            rx->i_acc += s * c;
            rx->q_acc -= s * si;
            rx->samples_in_symbol++;
        }
        rx->bit_pos++;

        rx->symbol_clock += 1.0f;
        if (rx->symbol_clock >= rx->symbol_period_samples) {
            rx->symbol_clock -= rx->symbol_period_samples;
            finish_simple_bpsk_symbol(rx);
            rx->bit_pos = 0;
        }
    }
}

void psk_rtty_rx_feed(psk_rtty_rx_t *rx, const int16_t *samples, size_t count)
{
    if (rx->config.mode == PSK_RTTY_MODE_BPSK31 || rx->config.mode == PSK_RTTY_MODE_PSK63) {
        simple_bpsk_rx_feed(rx, samples, count);
        return;
    }

    const float phase_inc = TWO_PI * (float)rx->config.rx_tone_hz / (float)rx->config.sample_rate_hz;
    for (size_t n = 0; n < count; ++n) {
        const float s = (float)samples[n] / 32768.0f;
        rx->i_acc += s * cosf(rx->nco_phase);
        rx->q_acc += s * sinf(rx->nco_phase);
        rx->samples_in_symbol++;
        rx->nco_phase += phase_inc;
        if (rx->nco_phase >= TWO_PI) {
            rx->nco_phase -= TWO_PI;
        }

        rx->symbol_clock += 1.0f;
        if (rx->symbol_clock >= rx->symbol_period_samples) {
            rx->symbol_clock -= rx->symbol_period_samples;
            finish_symbol(rx);
        }
    }
}
