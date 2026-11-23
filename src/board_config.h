#ifndef BOARD_CONFIG_H
#define BOARD_CONFIG_H

#include "hardware/structs/sysinfo.h"
#include "hardware/vreg.h"

//=============================================================================
// Debug Output Configuration
//=============================================================================
#ifndef FRANK_PRINCE_DEBUG
#define FRANK_PRINCE_DEBUG 0
#endif

#if FRANK_PRINCE_DEBUG
#define DBG_PRINTF(...) printf(__VA_ARGS__)
#else
#define DBG_PRINTF(...) ((void)0)
#endif

/*
 * Board Configuration Variants:
 * 
 * BOARD_M1 - M1 GPIO layout
 * BOARD_M2 - M2 GPIO layout
 * BOARD_OLIMEX - Olimex RP2350PC (Rev.A1, RP2350B QFN-80) layout
 * 
 * PSRAM pin is auto-detected based on chip package:
 *   RP2350B: GPIO47 (for both M1 and M2)
 *   RP2350A: GPIO19 (M1) or GPIO8 (M2)
 * BOARD_OLIMEX is fitted with an RP2350B, but its PSRAM (8MB APS6404L)
 * is hard-wired to QMI CS1 = GPIO8 (NOT GPIO47) -> fixed branch in
 * get_psram_pin() below, before the package auto-detection.
 * 
 * M1 GPIO Layout:
 *   HDMI: CLKN=6, CLKP=7, D0N=8, D0P=9, D1N=10, D1P=11, D2N=12, D2P=13
 *   SD:   CLK=2, CMD=3, DAT0=4, DAT3=5
 *   PS/2: CLK=0, DATA=1
 *   I2S:  DATA=26, CLK=27, LRCK=28
 * 
 * M2 GPIO Layout:
 *   HDMI: CLKN=12, CLKP=13, D0N=14, D0P=15, D1N=16, D1P=17, D2N=18, D2P=19
 *   SD:   CLK=6, CMD=7, DAT0=4, DAT3=5
 *   PS/2: CLK=2, DATA=3
 *   I2S:  DATA=9, CLK=10, LRCK=11
 *
 * OLIMEX RP2350PC GPIO Layout (Rev.A1, RP2350B QFN-80):
 *   HDMI: consecutive pairs, NON-sequential lane order:
 *         D0=12/13, CK=14/15 (middle!), D2=16/17, D1=18/19
 *         PIO serializer drives all 8 pins (12..19) as one 8-bit window;
 *         the clock pair is embedded in the data stream (bits 2/3).
 *   SD:   spi1: CS=9, SCK=10, MOSI(CMD)=11, MISO(DAT0)=24
 *   PS/2: none (GPIO0/1 = UEXT debug UART; the ps2kbd driver is NOT compiled)
 *   USB:  onboard 4-port hub (TinyUSB host, CFG_TUH_HUB=1)
 *   I2S:  DATA=31, BCLK=30, LRCK=29 (I2S_CLOCK_PIN_BASE=29 = LRCK below BCLK;
 *         NOTE for Phase 3 audio: LRCK sits UNDER the BCLK pin)
 *   PSRAM: 8MB APS6404L on QMI CS1 = GPIO8
 *
 * CPU/PSRAM Speed (set via CMake -DCPU_SPEED=xxx -DPSRAM_SPEED=xxx):
 *   252 MHz - no overclock (default for stable operation; exact PIO clkdiv 1.0)
 *   378 MHz - medium overclock
 *   504 MHz - high overclock
 */

// Default to M1 if no config specified
#if !defined(BOARD_M1) && !defined(BOARD_M2) && !defined(BOARD_OLIMEX)
#define BOARD_M1
#endif

//=============================================================================
// CPU/PSRAM Speed Defaults (can be overridden via CMake)
//=============================================================================
#ifndef CPU_CLOCK_MHZ
#define CPU_CLOCK_MHZ 252
#endif

#ifndef CPU_VOLTAGE
#define CPU_VOLTAGE VREG_VOLTAGE_1_50
#endif

#ifndef PSRAM_MAX_FREQ_MHZ
#define PSRAM_MAX_FREQ_MHZ 133
#endif

//=============================================================================
// PSRAM Pin Auto-Detection
//=============================================================================

// PSRAM pin for RP2350A variants
#ifdef BOARD_M1
#define PSRAM_PIN_RP2350A 19
#else
#define PSRAM_PIN_RP2350A 8
#endif

// PSRAM pin for RP2350B (always GPIO47)
#define PSRAM_PIN_RP2350B 47

// Runtime function to get PSRAM pin based on chip package
static inline uint get_psram_pin(void) {
#ifdef BOARD_OLIMEX
    // Olimex RP2350PC: 8MB APS6404L on QMI CS1 = GPIO8 (hard-wired on board).
    // Must come BEFORE the package auto-detection: the chip is an RP2350B,
    // which would otherwise wrongly report GPIO47.
    return 8;
#endif
    // Check if RP2350A (bit 0 set) or RP2350B (bit 0 clear)
    uint32_t package_sel = *((io_ro_32*)(SYSINFO_BASE + SYSINFO_PACKAGE_SEL_OFFSET));
    if (package_sel & 1) {
        // RP2350A - use board-specific pin
        return PSRAM_PIN_RP2350A;
    } else {
        // RP2350B - always GPIO47
        return PSRAM_PIN_RP2350B;
    }
}

//=============================================================================
// M1 Layout Configuration
//=============================================================================
#ifdef BOARD_M1

// HDMI Pins
#define HDMI_PIN_CLKN 6
#define HDMI_PIN_CLKP 7
#define HDMI_PIN_D0N  8
#define HDMI_PIN_D0P  9
#define HDMI_PIN_D1N  10
#define HDMI_PIN_D1P  11
#define HDMI_PIN_D2N  12
#define HDMI_PIN_D2P  13

#define HDMI_BASE_PIN HDMI_PIN_CLKN

// SD Card Pins
#define SDCARD_PIN_CLK    2
#define SDCARD_PIN_CMD    3
#define SDCARD_PIN_D0     4
#define SDCARD_PIN_D3     5

// PS/2 Keyboard Pins
#define PS2_PIN_CLK  0
#define PS2_PIN_DATA 1

// PS/2 Mouse Pins
#define PS2_MOUSE_CLK  14
#define PS2_MOUSE_DATA 15

// I2S Audio Pins
#define I2S_DATA_PIN       26
#define I2S_CLOCK_PIN_BASE 27

#endif // BOARD_M1

//=============================================================================
// M2 Layout Configuration
//=============================================================================
#ifdef BOARD_M2

// HDMI Pins
#define HDMI_PIN_CLKN 12
#define HDMI_PIN_CLKP 13
#define HDMI_PIN_D0N  14
#define HDMI_PIN_D0P  15
#define HDMI_PIN_D1N  16
#define HDMI_PIN_D1P  17
#define HDMI_PIN_D2N  18
#define HDMI_PIN_D2P  19

#define HDMI_BASE_PIN HDMI_PIN_CLKN

// SD Card Pins
#define SDCARD_PIN_CLK    6
#define SDCARD_PIN_CMD    7
#define SDCARD_PIN_D0     4
#define SDCARD_PIN_D3     5

// PS/2 Keyboard Pins
#define PS2_PIN_CLK  2
#define PS2_PIN_DATA 3

// PS/2 Mouse Pins
#define PS2_MOUSE_CLK  0
#define PS2_MOUSE_DATA 1

// I2S Audio Pins
#define I2S_DATA_PIN       9
#define I2S_CLOCK_PIN_BASE 10

#endif // BOARD_M2

//=============================================================================
// OLIMEX RP2350PC Layout Configuration (Rev.A1, RP2350B QFN-80)
//=============================================================================
#ifdef BOARD_OLIMEX

// HDMI Pins - consecutive pairs with NON-sequential lane order:
//   D0 = 12/13, CK = 14/15 (in the middle!), D2 = 16/17, D1 = 18/19
// The PIO video serializer drives ALL 8 pins (12..19) as a single 8-bit
// "out pins, 8" window (bit0 -> pin 12 ... bit7 -> pin 19); the clock pair
// is embedded in the serialized data stream at bits 2/3 (no PIO side-set).
#define HDMI_PIN_D0N 12
#define HDMI_PIN_D0P 13
#define HDMI_PIN_CKN 14
#define HDMI_PIN_CKP 15
#define HDMI_PIN_D2N 16
#define HDMI_PIN_D2P 17
#define HDMI_PIN_D1N 18
#define HDMI_PIN_D1P 19

// Base of the contiguous 8-pin serializer window (pins 12..19)
#define HDMI_BASE_PIN HDMI_PIN_D0N

// HDMI.h compatibility wrappers (HDMI.c uses these to set up the video SM).
// NOTE for OLIMEX the data bits are NOT one contiguous lane-ordered run:
// pins 12..19 = [D0-, D0+, CK-, CK+, D2-, D2+, D1-, D1+].
#define beginHDMI_PIN_data HDMI_PIN_D0N
#define beginHDMI_PIN_clk  HDMI_PIN_CKN

// SD Card Pins (spi1)
#define SDCARD_SPI_BUS spi1
#define SDCARD_PIN_CLK    10
#define SDCARD_PIN_CMD    11
#define SDCARD_PIN_D0     24
#define SDCARD_PIN_D3      9

// No PS/2 keyboard/mouse on this board (GPIO0/1 = UEXT debug UART; the
// ps2kbd driver is not compiled for BOARD_OLIMEX - see drivers/ps2kbd).

// I2S Audio Pins (Phase 3: codec ES8311) - NOT used by the PWM audio backend;
// left unconfigured in the OLIMEX build so the codec path stays untouched.
// NOTE: LRCK = I2S_CLOCK_PIN_BASE (29) sits BELOW BCLK (30) on this board.
#define I2S_DATA_PIN       31
#define I2S_CLOCK_PIN_BASE 29

// Audio backend selection:
//   - headphones (jack) = HP-out of the ES8311 codec -> I2S backend
//     (audio_i2s_driver + drivers/audio/es8311_init.c codec init)
//   - speakers = PWM class-D (GP26/27 -> RC filter -> PAM8003); the PWM
//     driver stays compiled for later use, it is just not selected.
#define AUDIO_BACKEND_PWM   0
#define PWM_AUDIO_PIN_BASE 26  // GP26 = A (left), GP27 = B (right)

// ES8311 codec wiring (headphone jack path)
#define CODEC_PWR_PIN 22  // LDO enable, driven HIGH by es8311_init()
#define CODEC_I2C_SDA 32
#define CODEC_I2C_SCL 33

// I2S clock wiring (Olimex RP2350PC): GP29 = LRCK, GP30 = BCLK — REVERSED
// vs M1/M2 (there: base = BCLK, base+1 = LRCK). audio_i2s_driver.c uses the
// side-set-swapped PIO variant (audio_i2s_lrck_low) with base = LRCK pin,
// so BCLK=GP30 / LRCK=GP29 reach the codec correctly. Data stays on GP31.
#define I2S_LRCK_BELOW_BCK 1

#endif // BOARD_OLIMEX

#endif // BOARD_CONFIG_H
