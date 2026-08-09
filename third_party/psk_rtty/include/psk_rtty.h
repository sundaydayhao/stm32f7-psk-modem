#pragma once

#include <stddef.h>
#include <stdint.h>

typedef enum {
    PSK_RTTY_MODE_BPSK31 = 0,
    PSK_RTTY_MODE_QPSK31_USB,
    PSK_RTTY_MODE_QPSK31_LSB,
    PSK_RTTY_MODE_RTTY,
    PSK_RTTY_MODE_PSK63,
    PSK_RTTY_MODE_COUNT
} psk_rtty_mode_t;

typedef void (*psk_rtty_char_cb_t)(char c, void *userdata);

typedef struct {
    psk_rtty_mode_t mode;
    int sample_rate_hz;
    int rx_tone_hz;
    psk_rtty_char_cb_t char_cb;
    void *userdata;
} psk_rtty_config_t;

typedef struct {
    float x;
    float y;
} psk_rtty_complex_t;

typedef struct {
    psk_rtty_config_t config;
    float nco_phase;
    float nco_phase_inc;
    float nco_cos;
    float nco_sin;
    float freq_error;
    float afc_limit;
    float afc_min;
    float afc_max;
    float fferr_ave;
    float fperr_ave;
    float q_freq_error;
    float nlp_k;
    float agc_ave;
    float dev_ave;
    float bit_phase_inc;
    float bit_phase_pos;
    float prev_symbol_phase;
    float symbol_clock;
    float symbol_period_samples;
    float i_acc;
    float q_acc;
    float sync_ave[21];
    float i0;
    float i1;
    float q0;
    float q1;
    psk_rtty_complex_t dec1_queue[35];
    psk_rtty_complex_t dec2_queue[35];
    psk_rtty_complex_t bit_queue[65];
    psk_rtty_complex_t freq_signal;
    psk_rtty_complex_t bit_signal;
    psk_rtty_complex_t z1;
    psk_rtty_complex_t z2;
    int samples_in_symbol;
    int sample_count;
    int afc_timer;
    int dec1_state;
    int dec2_state;
    int bit_fir_state;
    int pk_pos;
    int new_pk_pos;
    int raw_pk_pos;
    int bit_pos;
    int warmup_symbols;
    int last_pk_pos;
    int sq_open;
    int sq_was_open;
    int sq_stable_symbols;
    int sq_level;
    int sq_thresh;
    int squelch_speed;
    int on_count;
    int off_count;
    int p_count;
    int n_count;
    uint16_t bit_acc;
    int last_bit_zero;
    int rtty_figures;
    int rtty_bit_index;
    uint8_t rtty_shift_reg;
    uint32_t debug_bits;
    uint32_t debug_symbols;
    uint32_t debug_ones;
    uint32_t debug_zeros;
    uint32_t debug_delims;
    uint32_t debug_chars;
    uint32_t debug_unknown;
} psk_rtty_rx_t;

void psk_rtty_rx_init(psk_rtty_rx_t *rx, const psk_rtty_config_t *config);
void psk_rtty_rx_set_mode(psk_rtty_rx_t *rx, psk_rtty_mode_t mode);
void psk_rtty_rx_set_tone(psk_rtty_rx_t *rx, int tone_hz);
void psk_rtty_rx_feed(psk_rtty_rx_t *rx, const int16_t *samples, size_t count);
