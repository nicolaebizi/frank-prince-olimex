/*
 * frank-prince - PWM Audio Driver (Olimex RP2350PC)
 *
 * Class-D style audio output through the board's analog front-end:
 *   GP26 (PWM chan A, left) / GP27 (PWM chan B, right)
 *     -> onboard RC filter -> PAM8003 amp -> speakers.
 *
 * Architecture (mirrors audio_i2s_driver.c; the SDL callback still runs on
 * the main loop, the IRQ never touches game state):
 *
 *     pump() [main loop]  -->  staging ring (int16 stereo)  -->  DMA IRQ
 *       converts each int16 frame to a 32-bit CC write:
 *           CC = (dutyR << 16) | dutyL      (A=[15:0], B=[31:16])
 *       duty = clamp(512 + (sample >> 6), 0..1023)  (10-bit, mid = silence)
 *
 *   - PWM carrier: clkdiv 1.0, wrap 1023 -> 378 MHz / 1024 = 369.1 kHz
 *     (above audio band; filtered by the board's RC + PAM8003).
 *   - CC pacing: DMA_TIMER0 generates TREQs at clk_sys/8571 = 44100.07 Hz,
 *     so the DMA writes exactly one stereo frame per sample period. The PWM
 *     hardware keeps comparing every carrier cycle against the current CC,
 *     so no sample up-sampling is needed (deviation from the original plan
 *     of one CC write per carrier wrap, which would require 8.37x
 *     up-sampling of the 44.1 kHz stream).
 *   - Underrun: the IRQ fills the DMA buffer with mid-scale duty
 *     (0x02000200 = silence) instead of garbage.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef AUDIO_PWM_DRIVER_H
#define AUDIO_PWM_DRIVER_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

#if AUDIO_BACKEND_PWM
/*
 * API-selection aliases (see src/board_config.h): when the PWM backend is
 * selected, the SDL shim keeps calling the audio_i2s_driver_* names and they
 * resolve to this driver. Include AFTER audio_i2s_driver.h; never include
 * this header from audio_i2s_driver.c (the aliases would rename its
 * definitions too).
 */
#define audio_i2s_driver_init         audio_pwm_driver_init
#define audio_i2s_driver_pump         audio_pwm_driver_pump
#define audio_i2s_driver_set_enabled  audio_pwm_driver_set_enabled
#define audio_i2s_driver_is_enabled   audio_pwm_driver_is_enabled
#define audio_i2s_driver_shutdown     audio_pwm_driver_shutdown
#define audio_i2s_driver_lock         audio_pwm_driver_lock
#define audio_i2s_driver_unlock       audio_pwm_driver_unlock
#define audio_i2s_driver_get_silence  audio_pwm_driver_get_silence
#endif /* AUDIO_BACKEND_PWM */

// Audio sample rate (matching SDLPoP's default)
#ifndef AUDIO_SAMPLE_RATE
#define AUDIO_SAMPLE_RATE 44100
#endif

// Audio buffer configuration (same as the I2S driver)
#ifndef AUDIO_BUFFER_SAMPLES
#define AUDIO_BUFFER_SAMPLES 1024
#endif
#ifndef AUDIO_BUFFER_COUNT
#define AUDIO_BUFFER_COUNT 4
#endif

// Audio callback function type (matches SDL_AudioCallback)
typedef void (*audio_callback_fn)(void *userdata, uint8_t *stream, int len);

/**
 * Initialize the PWM audio driver (GP26/27, slice 13, DMA timer pacing).
 * @param sample_rate Sample rate in Hz (typically 44100)
 * @param channels Number of channels (2 = stereo L/R on GP26/GP27)
 * @param callback Audio callback function
 * @param userdata User data passed to callback
 * @return true if successful
 */
bool audio_pwm_driver_init(uint32_t sample_rate, uint8_t channels,
                           audio_callback_fn callback, void *userdata);

/**
 * Enable or disable audio playback. When disabled the staging ring drains
 * with silence (no DC kick through the amplifier).
 */
void audio_pwm_driver_set_enabled(bool enable);

/** Check if audio is currently enabled. */
bool audio_pwm_driver_is_enabled(void);

/** Shutdown the driver: stop IRQ/DMA, disable the PWM slice, release pins. */
void audio_pwm_driver_shutdown(void);

/** Silence value for the audio format (0 for signed 16-bit). */
uint8_t audio_pwm_driver_get_silence(void);

/** Lock audio to prevent callback execution (critical section). */
void audio_pwm_driver_lock(void);

/** Unlock audio after locking. */
void audio_pwm_driver_unlock(void);

/** Pump audio buffers from main loop (fills the staging ring). */
void audio_pwm_driver_pump(void);

#ifdef __cplusplus
}
#endif

#endif // AUDIO_PWM_DRIVER_H
