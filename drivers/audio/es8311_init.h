// ES8311 low-power stereo audio CODEC — bare-metal init for the Olimex RP2350PC
// (frank-prince port).
//
// Ported from the proven pico-speccy-olimex-rp2350pc donor driver; register
// map and slave-mode init sequence originally from Espressif's esp-bsp ES8311
// driver (components/es8311/es8311.c + priv_include/es8311_reg.h):
//   SPDX-License-Identifier: Apache-2.0
//   Copyright 2015-2026 Espressif Systems (Shanghai) CO LTD
//
// Configuration baked in (frank-prince):
//   - host is I2S MASTER (PIO, drivers/audio/audio_i2s_driver.c); the codec
//     runs in SLAVE serial-port mode
//   - fs = 44100 Hz; BCLK = 32 x fs = 1.4112 MHz (PIO at 64 x fs, 1 bit per
//     2 cycles); 16-bit I2S Philips frames
//   - NO MCLK pin: the codec derives its internal clock from SCLK/BCLK
//     (REG02 = 0x18: x8 multiplier -> dig_mclk = 11.2896 MHz = 256 x fs)
//   - GP22 = codec LDO enable, driven HIGH here before the I2C probe
//   - I2C addr 0x18 on GP32(SDA)/GP33(SCL), i2c0, 100 kHz
#pragma once
#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <hardware/gpio.h>

// Full ES8311 initialization via I2C (addr 0x18): GP22 LDO enable, probe
// (2x ACK), soft reset, clocks from BCLK, 16-bit I2S Philips, DAC powered up
// and un-muted, DAC volume 191 (-32 dB). Returns 0 on success, -1 if the
// codec does not ACK the probe or a register write/read fails, and leaves
// es8311_last_rb_reg/es8311_last_rb_val for readback mismatches.
int es8311_init(uint sda_pin, uint scl_pin);

// DAC digital volume (register 0x32). volume follows the firmware scale:
// 0 = loudest (~0 dB), 16 = silent (register 0x00). No-op until
// es8311_init() has succeeded.
void es8311_set_dac_volume(uint8_t volume);

// True once es8311_init() completed the register readback successfully.
bool es8311_ready(void);

// First failing readback after a non-zero es8311_init() return: register
// address and the value read (for the caller's failure log). Zero/zero on the
// happy path.
extern uint8_t es8311_last_rb_reg, es8311_last_rb_val;

#ifdef __cplusplus
}
#endif
