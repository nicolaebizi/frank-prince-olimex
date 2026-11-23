// ES8311 codec init for the Olimex RP2350PC (frank-prince port) — see
// es8311_init.h for the provenance (esp-bsp, Apache-2.0) and the fixed
// configuration (slave mode, MCLK from SCLK/BCLK, 16-bit I2S Philips).
//
// Adapted from pico-speccy-olimex-rp2350pc (donor, proven on hardware):
//   - fs = 44100 Hz (frank-prince I2S driver), BCLK = 32 x fs = 1.4112 MHz
//     (PIO at 64 x fs, 1 bit per 2 cycles — audio_i2s.pio)
//   - GP22 codec LDO enabled here (pico-speccy did it in its sound init)

#include <hardware/i2c.h>
#include <hardware/gpio.h>
#include <pico/time.h>
#include "es8311_init.h"

#define ES8311_I2C_ADDR 0x18 // CE pin pulled down

// Codec LDO enable (board schematic): GP22 HIGH powers the ES8311 rails
#ifndef CODEC_PWR_PIN
#define CODEC_PWR_PIN 22
#endif

// ES8311 register addresses (Espressif es8311_reg.h naming)
#define ES8311_RESET_REG00      0x00 // reset digital, csm, clock manager etc.
#define ES8311_CLK_MANAGER_REG01 0x01 // select clk src for mclk, enable clocks
#define ES8311_CLK_MANAGER_REG02 0x02 // clk pre-divider and clk multiplier
#define ES8311_CLK_MANAGER_REG03 0x03 // adc fs_mode and osr
#define ES8311_CLK_MANAGER_REG04 0x04 // dac osr
#define ES8311_CLK_MANAGER_REG05 0x05 // clk divider for adc and dac
#define ES8311_CLK_MANAGER_REG06 0x06 // sclk (bclk) inverter and divider
#define ES8311_CLK_MANAGER_REG07 0x07 // tri-state, adc lrck divider
#define ES8311_CLK_MANAGER_REG08 0x08 // dac lrck divider
#define ES8311_SDPIN_REG09      0x09 // dac serial digital port (I2S in)
#define ES8311_SDPOUT_REG0A     0x0A // adc serial digital port (I2S out)
#define ES8311_SYSTEM_REG0D     0x0D // system, power up/down
#define ES8311_SYSTEM_REG0E     0x0E // system, power up/down
#define ES8311_SYSTEM_REG12     0x12 // system, enable DAC
#define ES8311_SYSTEM_REG13     0x13 // system, HP drive
#define ES8311_ADC_REG1C        0x1C // ADC equalizer, hpf (ADC unused here)
#define ES8311_DAC_REG31        0x31 // DAC mute
#define ES8311_DAC_REG32        0x32 // DAC volume
#define ES8311_DAC_REG37        0x37 // DAC ramp rate / equalizer

// Post-init DAC volume: 0x00 = -inf .. 0xFF = 0 dB (~0.5 dB/step).
// 191 = -32 dB: the proven comfortable cap for the PAM8003 path.
#define ES8311_INIT_VOLUME 191

static i2c_inst_t *es8311_i2c = NULL;
static bool es8311_is_ready = false;
// First failing readback (for the caller's log)
uint8_t es8311_last_rb_reg = 0, es8311_last_rb_val = 0;

static i2c_inst_t *get_i2c_inst(uint sda_pin) {
    // Even GPIO pairs (0/1, 4/5, 8/9...) -> i2c0 (32/33 -> i2c0, as wired).
    return ((sda_pin >> 1) & 1) ? i2c1 : i2c0;
}

static bool es8311_write_reg(uint8_t reg, uint8_t val) {
    const uint8_t buf[2] = { reg, val };
    return i2c_write_blocking(es8311_i2c, ES8311_I2C_ADDR, buf, 2, false) == 2;
}

static bool es8311_read_reg(uint8_t reg, uint8_t *val) {
    // Repeated-start read: write the register address with nostop=true, then
    // read back in the same transaction.
    if (i2c_write_blocking(es8311_i2c, ES8311_I2C_ADDR, &reg, 1, true) != 1)
        return false;
    return i2c_read_blocking(es8311_i2c, ES8311_I2C_ADDR, val, 1, false) == 1;
}

// Two clean ACKs, not one — same rationale as pcm5122_detect(): a residual
// glitch on the shared DDC FET lines must not be misread as the codec.
static bool es8311_probe(void) {
    uint8_t rxdata;
    for (int i = 0; i < 2; ++i) {
        if (i2c_read_timeout_us(es8311_i2c, ES8311_I2C_ADDR, &rxdata, 1, false, 5000) < 0)
            return false;
    }
    return true;
}

static bool es8311_i2c_setup(uint sda_pin, uint scl_pin) {
    es8311_i2c = get_i2c_inst(sda_pin);
    i2c_init(es8311_i2c, 100000);
    // GPIO_FUNC_I2C is open-drain: safe next to the HDMI DDC FETs on GP32/33.
    gpio_set_function(sda_pin, GPIO_FUNC_I2C);
    gpio_set_function(scl_pin, GPIO_FUNC_I2C);
    // Board carries 2.2k pull-ups; the internal ones only help at probe time.
    gpio_pull_up(sda_pin);
    gpio_pull_up(scl_pin);
    return true;
}

int es8311_init(uint sda_pin, uint scl_pin) {
    es8311_is_ready = false;

    // Enable the codec LDO first (GP22 HIGH), then give the rails time to
    // come up before the I2C probe.
    gpio_set_function(CODEC_PWR_PIN, GPIO_FUNC_SIO);
    gpio_set_dir(CODEC_PWR_PIN, GPIO_OUT);
    gpio_put(CODEC_PWR_PIN, 1);
    sleep_ms(20);

    if (!es8311_i2c) {
        es8311_i2c_setup(sda_pin, scl_pin);
    }

    if (!es8311_probe()) {
        return -1;
    }

    // ---- Reset (esp-bsp es8311_init) ----
    // 0x1F = reset the digital block / clock manager, then power-on 0x80.
    if (!es8311_write_reg(ES8311_RESET_REG00, 0x1F)) return -1;
    sleep_ms(20);
    if (!es8311_write_reg(ES8311_RESET_REG00, 0x00)) return -1;
    if (!es8311_write_reg(ES8311_RESET_REG00, 0x80)) return -1; // power-on

    // ---- Clocks: source BCLK (a.k.a. SCLK), no MCLK pin ----
    // REG01 = 0x3F (enable all codec clocks) | BIT7 = internal MCLK taken from
    // the SCLK/BCLK pin (esp-bsp: mclk_from_mclk_pin = false). mclk_int = BCLK
    // = 32 x fs; 16-bit in == 16-bit out is required for that mode.
    if (!es8311_write_reg(ES8311_CLK_MANAGER_REG01, 0xBF)) return -1;

    // REG02 for frank-prince: fs = 44100 Hz, BCLK = 32 x fs = 1.4112 MHz.
    // The DAC needs dig_mclk >= 256 x fs = 11.2896 MHz; multiplier x8 on
    // BCLK gives exactly 11.2896 MHz (= 256 x fs), and BCLK is low enough
    // that no extra pre-division is needed (pre_div = 1).
    //   REG02 = (pre_div - 1) << 5 | 8x-mult-selector(3) << 3 = 0x18
    // (matches the esp-bsp ratio row {11289600, 44100}; same value the donor
    // used for its 32fs stream — x8, not the RP2350PC_I2S_64FS x4 variant,
    // because at BCLK 2 MHz x8 would exceed the mclk limit while at
    // 1.4112 MHz it lands exactly on 256 x fs).
    if (!es8311_write_reg(ES8311_CLK_MANAGER_REG02, (1 - 1) << 5 | 0x03 << 3)) return -1;
    // REG03: fs_mode = single speed (<<6), ADC OSR = 16
    if (!es8311_write_reg(ES8311_CLK_MANAGER_REG03, 0x00 << 6 | 0x10)) return -1;
    // REG04: DAC OSR = 16
    if (!es8311_write_reg(ES8311_CLK_MANAGER_REG04, 0x10)) return -1;
    // REG05: ADC divider 1, DAC divider 1
    if (!es8311_write_reg(ES8311_CLK_MANAGER_REG05, (1 - 1) << 4 | (1 - 1))) return -1;
    // REG06: SCLK not inverted (bit5 = 0), BCLK divider = master-mode only;
    // in slave mode BCLK is an input, keep the esp-bsp table value (div 4).
    if (!es8311_write_reg(ES8311_CLK_MANAGER_REG06, 0x03)) return -1;
    // REG07/08: ADC/DAC LRCK divider — used for master-mode LRCK generation
    // only; in slave mode LRCK is an input. esp-bsp table values.
    if (!es8311_write_reg(ES8311_CLK_MANAGER_REG07, 0x00)) return -1;
    if (!es8311_write_reg(ES8311_CLK_MANAGER_REG08, 0xFF)) return -1;

    // ---- Serial port format: slave, I2S (Philips), 16/16 bit ----
    // REG00 bit6 = 0: serial port in slave mode (host PIO is the master).
    uint8_t reg00;
    if (!es8311_read_reg(ES8311_RESET_REG00, &reg00)) return -1;
    if (!es8311_write_reg(ES8311_RESET_REG00, reg00 & ~0x40)) return -1;
    // REG09/0A: bits [5:4] = 00 -> I2S Philips format; bits [3:2] = 3 -> 16-bit
    if (!es8311_write_reg(ES8311_SDPIN_REG09, 0x0C)) return -1;
    if (!es8311_write_reg(ES8311_SDPOUT_REG0A, 0x0C)) return -1;

    // ---- Power up the DAC side ----
    if (!es8311_write_reg(ES8311_SYSTEM_REG0D, 0x01)) return -1; // power up analog circuitry
    if (!es8311_write_reg(ES8311_SYSTEM_REG0E, 0x02)) return -1; // enable analog PGA + ADC modulator
    if (!es8311_write_reg(ES8311_SYSTEM_REG12, 0x00)) return -1; // power up DAC
    if (!es8311_write_reg(ES8311_SYSTEM_REG13, 0x10)) return -1; // enable output to HP drive
    if (!es8311_write_reg(ES8311_ADC_REG1C, 0x6A))    return -1; // ADC eq bypass, DC-offset cancel

    // ---- DAC un-mute, moderate volume, eq bypass ----
    // REG31: clear the soft-mute bits (5/6)
    uint8_t reg31;
    if (!es8311_read_reg(ES8311_DAC_REG31, &reg31)) return -1;
    if (!es8311_write_reg(ES8311_DAC_REG31, reg31 & ~0x60)) return -1;
    // REG32: DAC digital volume, 0x00 = -inf .. 0xFF = 0 dB (~0.5 dB/step).
    // 191 = -32 dB: the proven comfortable cap (donor used 0x66 = quieter).
    if (!es8311_write_reg(ES8311_DAC_REG32, ES8311_INIT_VOLUME)) return -1;
    // REG37: DAC ramp/eq — bypass equalizer (esp-bsp), default fade rate
    if (!es8311_write_reg(ES8311_DAC_REG37, 0x08)) return -1;

    // ---- Readback check: the registers that define the I2S interface ----
    static const struct { uint8_t reg; uint8_t want; } check_regs[] = {
        { ES8311_CLK_MANAGER_REG01, 0xBF }, // MCLK src = BCLK, clocks on
        { ES8311_CLK_MANAGER_REG02, 0x18 }, // pre-div 1, multi 8x (256 x fs @ BCLK 1.4112 MHz)
        { ES8311_RESET_REG00,       0x80 }, // power on, serial port = SLAVE (bit6 = 0)
        { ES8311_SDPIN_REG09,       0x0C }, // I2S Philips, 16-bit in
        { ES8311_SDPOUT_REG0A,      0x0C }, // I2S Philips, 16-bit out
        { ES8311_SYSTEM_REG12,      0x00 }, // DAC powered up
        { ES8311_DAC_REG32,         ES8311_INIT_VOLUME },
    };
    for (unsigned i = 0; i < sizeof(check_regs) / sizeof(check_regs[0]); ++i) {
        uint8_t rb;
        if (!es8311_read_reg(check_regs[i].reg, &rb)) return -1;
        if (rb != check_regs[i].want) {
            es8311_last_rb_reg = check_regs[i].reg;
            es8311_last_rb_val = rb;
            return -1;
        }
    }

    es8311_is_ready = true;
    return 0;
}

void es8311_set_dac_volume(uint8_t volume) {
    if (!es8311_i2c) return;
    // Firmware scale 0 (loudest) .. 16 (silent) -> REG32 0xFF (0 dB) .. 0x00.
    int reg = 255 - (int)volume * 16;
    if (reg < 0) reg = 0;
    es8311_write_reg(ES8311_DAC_REG32, (uint8_t)reg);
}

bool es8311_ready(void) {
    return es8311_is_ready;
}
