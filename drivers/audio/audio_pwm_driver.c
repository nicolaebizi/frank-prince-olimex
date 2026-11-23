/*
 * frank-prince - PWM Audio Driver Implementation (Olimex RP2350PC)
 * Mirrors audio_i2s_driver.c: staging ring on the main loop, DMA ping-pong
 * with IRQ refill, silence on underrun. Output = 10-bit PWM duty on GP26/27
 * (slice 13) into the board's RC filter + PAM8003 amp.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "audio_pwm_driver.h"
#include "board_config.h"

#include "pico/stdlib.h"
#include "pico/sync.h"
#include "hardware/gpio.h"
#include "hardware/pwm.h"
#include "hardware/dma.h"
#include "hardware/clocks.h"
#include "hardware/irq.h"
#include <string.h>
#include <stdio.h>

// ============================================================================
// Configuration
// ============================================================================

// PWM pins: GP26 = channel A (left), GP27 = channel B (right), slice 13
#ifndef PWM_AUDIO_PIN_L
#define PWM_AUDIO_PIN_L 26
#endif
#ifndef PWM_AUDIO_PIN_R
#define PWM_AUDIO_PIN_R 27
#endif

// Carrier: clk_sys / (PWM_TOP + 1); 378 MHz / 1024 = 369.1 kHz (inaudible)
#define PWM_AUDIO_TOP 1023

// DMA channels for ping-pong (same pair as the I2S driver; only one audio
// driver is active per build, so no conflict)
#define AUDIO_DMA_CH_A    10
#define AUDIO_DMA_CH_B    11
#define AUDIO_DMA_IRQ_NUM DMA_IRQ_0

// DMA pacing timer: TREQ every (X+1)*(Y+1) clk_sys cycles. We target one
// stereo-frame write per sample period: clk_sys/44100 = 8571 @ 378 MHz.
#define AUDIO_DMA_TIMER_INDEX 0

// DMA buffer: stereo frames (one uint32_t = CC write = L in [15:0], R in [31:16])
#define DMA_BUFFER_COUNT      2
#define DMA_BUFFER_MAX_FRAMES AUDIO_BUFFER_SAMPLES

// Staging ring: filled by main-loop pump(), consumed by DMA IRQ.
#define STAGING_COUNT 4

// Silence duty (mid-scale) for both channels: 512 | 512<<16
#define PWM_CC_SILENCE 0x02000200u

// ============================================================================
// State
// ============================================================================

// DMA ping-pong buffers (written by IRQ only) - hold CC write values
static uint32_t __attribute__((aligned(4)))
    dma_buffers[DMA_BUFFER_COUNT][DMA_BUFFER_MAX_FRAMES];

// Staging ring (written by main loop, read by IRQ) - int16 stereo frames
static uint32_t __attribute__((aligned(4)))
    staging[STAGING_COUNT][DMA_BUFFER_MAX_FRAMES];
static volatile uint8_t staging_wr = 0;  // next slot main loop writes
static volatile uint8_t staging_rd = 0;  // next slot IRQ reads

static int  dma_channel_a = -1;
static int  dma_channel_b = -1;
static uint pwm_slice;
static uint32_t dma_transfer_count;
static uint32_t dma_timer_setting;

static volatile bool audio_running = false;

static struct {
    bool initialized;
    volatile bool enabled;
    uint32_t sample_rate;
    uint8_t channels;
    audio_callback_fn callback;
    void *userdata;
    critical_section_t lock;
} audio_state = {0};

static void audio_dma_irq_handler(void);

// ============================================================================
// Staging ring helpers
// ============================================================================

static inline uint8_t staging_avail(void) {
    return (uint8_t)(staging_wr - staging_rd);  // works with wrapping uint8_t
}

static inline bool staging_full(void) {
    return staging_avail() >= STAGING_COUNT;
}

// ============================================================================
// Internal: PWM + DMA setup
// ============================================================================

static bool audio_hw_init(uint32_t sample_rate) {
    // GPIOs to PWM function (slice 13 for GP26/27)
    gpio_set_function(PWM_AUDIO_PIN_L, GPIO_FUNC_PWM);
    gpio_set_function(PWM_AUDIO_PIN_R, GPIO_FUNC_PWM);
    pwm_slice = pwm_gpio_to_slice_num(PWM_AUDIO_PIN_L);

    // Carrier: clkdiv 1.0 (full sys clock), wrap 1023 -> 10-bit duty
    pwm_set_clkdiv(pwm_slice, 1.0f);
    pwm_set_wrap(pwm_slice, PWM_AUDIO_TOP);
    pwm_set_both_levels(pwm_slice, 512, 512);  // start at mid-scale (silence)
    pwm_set_enabled(pwm_slice, true);

    // DMA pacing timer: one TREQ per sample period (44100 Hz @ 378 MHz
    // => 8571 cycles = 44100.07 Hz, +1.6 ppm)
    uint32_t sys_clk = clock_get_hz(clk_sys);
    uint32_t div = sys_clk / sample_rate;
    if (div == 0) div = 1;
    if (div > 65536) div = 65536;
    dma_timer_setting = (0u << 16) | (uint32_t)(div - 1);
    dma_hw->timer[AUDIO_DMA_TIMER_INDEX] = dma_timer_setting;

    // Silence all buffers
    for (uint32_t i = 0; i < DMA_BUFFER_MAX_FRAMES; i++) {
        dma_buffers[0][i] = PWM_CC_SILENCE;
        dma_buffers[1][i] = PWM_CC_SILENCE;
    }
    memset(staging, 0, sizeof(staging));
    staging_wr = 0;
    staging_rd = 0;

    dma_transfer_count = DMA_BUFFER_MAX_FRAMES;

    // Claim DMA channels
    dma_channel_abort(AUDIO_DMA_CH_A);
    dma_channel_abort(AUDIO_DMA_CH_B);
    while (dma_channel_is_busy(AUDIO_DMA_CH_A) ||
           dma_channel_is_busy(AUDIO_DMA_CH_B)) {
        tight_loop_contents();
    }
    dma_channel_unclaim(AUDIO_DMA_CH_A);
    dma_channel_unclaim(AUDIO_DMA_CH_B);
    dma_channel_claim(AUDIO_DMA_CH_A);
    dma_channel_claim(AUDIO_DMA_CH_B);
    dma_channel_a = AUDIO_DMA_CH_A;
    dma_channel_b = AUDIO_DMA_CH_B;

    // Configure DMA channels in ping-pong chain, paced by DMA_TIMER0,
    // writing directly into the slice's CC register (A=[15:0], B=[31:16])
    volatile void *cc_reg = (volatile void *)&pwm_hw->slice[pwm_slice].cc;

    dma_channel_config cfg_a = dma_channel_get_default_config(dma_channel_a);
    channel_config_set_read_increment(&cfg_a, true);
    channel_config_set_write_increment(&cfg_a, false);
    channel_config_set_transfer_data_size(&cfg_a, DMA_SIZE_32);
    channel_config_set_dreq(&cfg_a, DREQ_DMA_TIMER0 + AUDIO_DMA_TIMER_INDEX);
    channel_config_set_chain_to(&cfg_a, dma_channel_b);

    dma_channel_config cfg_b = dma_channel_get_default_config(dma_channel_b);
    channel_config_set_read_increment(&cfg_b, true);
    channel_config_set_write_increment(&cfg_b, false);
    channel_config_set_transfer_data_size(&cfg_b, DMA_SIZE_32);
    channel_config_set_dreq(&cfg_b, DREQ_DMA_TIMER0 + AUDIO_DMA_TIMER_INDEX);
    channel_config_set_chain_to(&cfg_b, dma_channel_a);

    dma_channel_configure(dma_channel_a, &cfg_a,
                          cc_reg, dma_buffers[0], dma_transfer_count, false);
    dma_channel_configure(dma_channel_b, &cfg_b,
                          cc_reg, dma_buffers[1], dma_transfer_count, false);

    // Set up DMA IRQ handler (same IRQ line as the I2S driver would use;
    // only one audio driver is active per build)
    dma_hw->ints0 = (1u << dma_channel_a) | (1u << dma_channel_b);
    irq_set_exclusive_handler(AUDIO_DMA_IRQ_NUM, audio_dma_irq_handler);
    irq_set_priority(AUDIO_DMA_IRQ_NUM, 0x80);
    irq_set_enabled(AUDIO_DMA_IRQ_NUM, true);
    dma_channel_set_irq0_enabled(dma_channel_a, true);
    dma_channel_set_irq0_enabled(dma_channel_b, true);

    audio_running = false;
    return true;
}

static void audio_hw_deinit(void) {
    audio_running = false;

    irq_set_enabled(AUDIO_DMA_IRQ_NUM, false);

    if (dma_channel_a >= 0) {
        dma_channel_set_irq0_enabled(dma_channel_a, false);
        dma_channel_abort(dma_channel_a);
        dma_hw->ints0 = (1u << dma_channel_a);
        dma_channel_unclaim(dma_channel_a);
        dma_channel_a = -1;
    }

    if (dma_channel_b >= 0) {
        dma_channel_set_irq0_enabled(dma_channel_b, false);
        dma_channel_abort(dma_channel_b);
        dma_hw->ints0 = (1u << dma_channel_b);
        dma_channel_unclaim(dma_channel_b);
        dma_channel_b = -1;
    }

    // Disable the PWM slice and release the pins to high-Z SIO inputs
    // (the RC filter / PAM8003 input floats to its own bias point; driving
    // them LOW would present a DC level to the amplifier)
    pwm_set_both_levels(pwm_slice, 512, 512);
    pwm_set_enabled(pwm_slice, false);
    gpio_set_function(PWM_AUDIO_PIN_L, GPIO_FUNC_SIO);
    gpio_set_function(PWM_AUDIO_PIN_R, GPIO_FUNC_SIO);
    gpio_set_dir(PWM_AUDIO_PIN_L, GPIO_IN);
    gpio_set_dir(PWM_AUDIO_PIN_R, GPIO_IN);
}

// ============================================================================
// DMA IRQ handler - converts the next staging buffer to CC values, re-arms
//
// Never calls the SDL callback. Conversion per stereo frame:
//   duty = clamp(512 + (sample >> 6), 0..1023); CC = (dutyR << 16) | dutyL.
// Underrun: mid-scale duty (silence), never garbage.
// ============================================================================

static inline void copy_to_cc(uint32_t *dst, const uint32_t *src, uint32_t frames) {
    const int16_t *s = (const int16_t *)src;
    for (uint32_t i = 0; i < frames; i++) {
        int dl = 512 + (s[2 * i] >> 6);
        int dr = 512 + (s[2 * i + 1] >> 6);
        if (dl < 0) dl = 0; else if (dl > 1023) dl = 1023;
        if (dr < 0) dr = 0; else if (dr > 1023) dr = 1023;
        dst[i] = ((uint32_t)dr << 16) | (uint32_t)dl;
    }
}

static void audio_dma_irq_handler(void) {
    uint32_t ints = dma_hw->ints0;
    uint32_t mask = 0;
    if (dma_channel_a >= 0) mask |= (1u << dma_channel_a);
    if (dma_channel_b >= 0) mask |= (1u << dma_channel_b);
    ints &= mask;
    if (!ints) return;

    if ((dma_channel_a >= 0) && (ints & (1u << dma_channel_a))) {
        dma_hw->ints0 = (1u << dma_channel_a);
        if (staging_avail() > 0) {
            copy_to_cc(dma_buffers[0], staging[staging_rd % STAGING_COUNT], dma_transfer_count);
            staging_rd++;
        } else {
            for (uint32_t i = 0; i < dma_transfer_count; i++) dma_buffers[0][i] = PWM_CC_SILENCE;
        }
        __dmb();
        dma_channel_set_read_addr(dma_channel_a, dma_buffers[0], false);
        dma_channel_set_trans_count(dma_channel_a, dma_transfer_count, false);
    }

    if ((dma_channel_b >= 0) && (ints & (1u << dma_channel_b))) {
        dma_hw->ints0 = (1u << dma_channel_b);
        if (staging_avail() > 0) {
            copy_to_cc(dma_buffers[1], staging[staging_rd % STAGING_COUNT], dma_transfer_count);
            staging_rd++;
        } else {
            for (uint32_t i = 0; i < dma_transfer_count; i++) dma_buffers[1][i] = PWM_CC_SILENCE;
        }
        __dmb();
        dma_channel_set_read_addr(dma_channel_b, dma_buffers[1], false);
        dma_channel_set_trans_count(dma_channel_b, dma_transfer_count, false);
    }
}

// ============================================================================
// Public API Implementation
// ============================================================================

bool audio_pwm_driver_init(uint32_t sample_rate, uint8_t channels,
                           audio_callback_fn callback, void *userdata) {
    if (audio_state.initialized) {
        DBG_PRINTF("audio_pwm_driver: already initialized\n");
        return false;
    }

    DBG_PRINTF("audio_pwm_driver: initializing (rate=%lu, ch=%d, slice=%u)\n",
           (unsigned long)sample_rate, channels, pwm_gpio_to_slice_num(PWM_AUDIO_PIN_L));

    audio_state.sample_rate = sample_rate;
    audio_state.channels = channels;
    audio_state.callback = callback;
    audio_state.userdata = userdata;

    critical_section_init(&audio_state.lock);

    if (!audio_hw_init(sample_rate)) {
        DBG_PRINTF("audio_pwm_driver: hardware init failed\n");
        return false;
    }

    audio_state.initialized = true;
    audio_state.enabled = false; // Paused until SDL_PauseAudio(0)

    DBG_PRINTF("audio_pwm_driver: initialization complete (slice %u, DMA %d/%d, timer pace %lu Hz)\n",
           pwm_slice, AUDIO_DMA_CH_A, AUDIO_DMA_CH_B,
           (unsigned long)(clock_get_hz(clk_sys) / (dma_timer_setting & 0xffffu ? (dma_timer_setting & 0xffffu) + 1 : 1)));
    return true;
}

void audio_pwm_driver_set_enabled(bool enable) {
    if (!audio_state.initialized) return;

    if (enable && !audio_state.enabled) {
        DBG_PRINTF("audio_pwm_driver: unpausing audio\n");
        audio_state.enabled = true;
    } else if (!enable && audio_state.enabled) {
        DBG_PRINTF("audio_pwm_driver: pausing audio\n");
        audio_state.enabled = false;  // staging ring drains with silence
    }
}

bool audio_pwm_driver_is_enabled(void) {
    return audio_state.enabled;
}

void audio_pwm_driver_shutdown(void) {
    if (!audio_state.initialized) return;

    DBG_PRINTF("audio_pwm_driver: shutting down\n");
    audio_state.enabled = false;

    audio_hw_deinit();

    audio_state.initialized = false;
}

uint8_t audio_pwm_driver_get_silence(void) {
    return 0; // Signed 16-bit format uses 0 for silence
}

void audio_pwm_driver_lock(void) {
    if (audio_state.initialized) {
        critical_section_enter_blocking(&audio_state.lock);
    }
}

void audio_pwm_driver_unlock(void) {
    if (audio_state.initialized) {
        critical_section_exit(&audio_state.lock);
    }
}

// Pump: fill staging buffers from the SDL callback on the main loop,
// then start DMA on the first call once enough data is queued.
void audio_pwm_driver_pump(void) {
    if (!audio_state.initialized) return;

    int buffer_bytes = (int)(dma_transfer_count * sizeof(uint32_t));

    // Fill as many staging slots as are free
    while (!staging_full()) {
        uint32_t *slot = staging[staging_wr % STAGING_COUNT];

        if (audio_state.enabled && audio_state.callback) {
            audio_state.callback(audio_state.userdata, (uint8_t *)slot, buffer_bytes);
        } else {
            memset(slot, 0, buffer_bytes);
        }
        __dmb();
        staging_wr++;
    }

    // Start DMA once we have enough pre-roll data
    if (!audio_running && staging_avail() >= DMA_BUFFER_COUNT) {
        // Prime both DMA buffers from the staging ring
        copy_to_cc(dma_buffers[0], staging[staging_rd % STAGING_COUNT],
                   dma_transfer_count);
        staging_rd++;
        copy_to_cc(dma_buffers[1], staging[staging_rd % STAGING_COUNT],
                   dma_transfer_count);
        staging_rd++;
        __dmb();

        audio_running = true;
        dma_channel_start(dma_channel_a);
    }
}
