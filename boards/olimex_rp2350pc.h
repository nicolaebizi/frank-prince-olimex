/*
 * Olimex RP2350PC (Rev.A1) board definition for frank-prince.
 *
 * Modeled on pico-sdk's boards/pimoroni_pico_plus2_rp2350.h (also an RP2350B board).
 *
 * Resolution: the SDK's cmake/generic_board.cmake searches <PICO_BOARD>.h in
 * PICO_BOARD_HEADER_DIRS; the project CMakeLists.txt appends ${CMAKE_CURRENT_LIST_DIR}/boards
 * to PICO_BOARD_HEADER_DIRS before pico_sdk_init(), so -DPICO_BOARD=olimex_rp2350pc
 * finds this file. The header is added to PICO_CONFIG_HEADER_FILES and dumped into the
 * generated pico/config_autogen.h (included by pico.h in every compilation unit).
 *
 * NOTE: THIS HEADER IS ALSO INCLUDED BY ASSEMBLER SO
 *       SHOULD ONLY CONSIST OF PREPROCESSOR DIRECTIVES
 */

#ifndef _BOARDS_OLIMEX_RP2350PC_H
#define _BOARDS_OLIMEX_RP2350PC_H

pico_board_cmake_set(PICO_PLATFORM, rp2350)

// For board detection
#define OLIMEX_RP2350PC

// --- RP2350 VARIANT ---
// Olimex RP2350PC is fitted with RP2350B (QFN-80, 48 GPIOs) => PICO_RP2350A = 0.
// (The SDK has no "PICO_RP2350B" macro; PICO_RP2350A==0 selects the B package.)
#define PICO_RP2350A 0

// --- UART: UEXT connector used as debug console; debug TX on GPIO0, RX on GPIO1 ---
#ifndef PICO_DEFAULT_UART
#define PICO_DEFAULT_UART 0
#endif
#ifndef PICO_DEFAULT_UART_TX_PIN
#define PICO_DEFAULT_UART_TX_PIN 0
#endif
#ifndef PICO_DEFAULT_UART_RX_PIN
#define PICO_DEFAULT_UART_RX_PIN 1
#endif

// --- FLASH: 16 MiB onboard ---
#define PICO_BOOT_STAGE2_CHOOSE_W25Q080 1

#ifndef PICO_FLASH_SPI_CLKDIV
#define PICO_FLASH_SPI_CLKDIV 2
#endif

pico_board_cmake_set_default(PICO_FLASH_SIZE_BYTES, (16 * 1024 * 1024))
#ifndef PICO_FLASH_SIZE_BYTES
#define PICO_FLASH_SIZE_BYTES (16 * 1024 * 1024)
#endif

// --- PSRAM: 8 MiB APS6404L, CS wired to QMI CS1 = GPIO8 (verified on board @252 MHz) ---
#ifndef PICO_PSRAM_CS_PIN
#define PICO_PSRAM_CS_PIN 8
#endif

pico_board_cmake_set_default(PICO_PSRAM_SIZE_BYTES, (8 * 1024 * 1024))
#ifndef PICO_PSRAM_SIZE_BYTES
#define PICO_PSRAM_SIZE_BYTES (8 * 1024 * 1024)
#endif

// 12 MHz crystal is the pico-sdk default (XOSC_KHZ = 12000); no override required.

// Rev.A1 boards ship with RP2350 "A2" silicon stepping
#ifndef PICO_RP2350_A2_SUPPORTED
#define PICO_RP2350_A2_SUPPORTED 1
#endif

#endif
