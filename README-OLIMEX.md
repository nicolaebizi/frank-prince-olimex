# Frank-Prince pentru Olimex RP2350PC (Rev.A1) / Frank-Prince for Olimex RP2350PC (Rev.A1)

Port complet al **frank-prince** (Prince of Persia pe RP2350, SDLPoP) pe placa
**Olimex RP2350PC** — HW-validat: video stabil, culori corecte, sunet în jack
pe codec-ul ES8311.

A complete port of **frank-prince** (Prince of Persia on RP2350, SDLPoP) to the
**Olimex RP2350PC** board — hardware-validated: stable video, correct colors,
working headphone output through the ES8311 codec.

---

## Configurare hardware / Hardware configuration

| Subsistem | Detalii |
|---|---|
| MCU | RP2350B (QFN-80), 12 MHz crystal, flash 16 MB, **CPU 378 MHz** / **PSRAM 133 MHz** (QMI: 126 MHz pe APS6404L) |
| HDMI | PIO `out pins, 8` — perechi consecutive cu ordine neregulată de lane-uri: **D0=12/13, CK=14/15, D2=16/17, D1=18/19**; polaritate dif-pairs `invert_diffpairs=0` (fix culori) |
| PSRAM | 8 MB APS6404L pe QMI **CS1 = GPIO8** |
| SD | **spi1**: CS=9, SCK=10, MOSI=11, MISO=24 |
| Tastatură | hub USB integrat 4×A — TinyUSB **host** (`CFG_TUH_HUB=1`); PS/2 neprezent (GPIO0/1 = UEXT debug UART) |
| Audio (jack) | I2S: LRCK=**GP29**, BCLK=**GP30**, DATA=**GP31**; codec **ES8311** — I2C 0x18 pe GP32/33, LDO pe **GP22**; **REG02=0x18** (mclk = 256×fs @ BCLK 32×fs); PIO variantă **`audio_i2s_lrck_low`** (side-set swapat — schema OLIMEX are LRCK sub BCLK, invers față de M1/M2) |
| Audio (difuzoare) | driver **PWM** GP26/27 → filtru RC → PAM8003 — disponibil prin `AUDIO_BACKEND_PWM=1` (în `src/board_config.h`); backend implicit = I2S/ES8311 |
| Video ISR | întregul lanț ISR per-linie (`dma_handler_HDMI` + helpers) în **SRAM** (`__time_critical_func`) |

## Build

```bash
cmake -DPICO_SDK_PATH=/path/to/pico-sdk \
      -DPICO_BOARD=olimex_rp2350pc \
      -DBOARD_VARIANT=OLIMEX \
      -DCPU_SPEED=378 -DPSRAM_SPEED=133 \
      -DUSB_HID_ENABLED=1 \
      -DCMAKE_BUILD_TYPE=Release \
      -G Ninja -B build-olimex -S .
cmake --build build-olimex --parallel 6
```

Header-ul de placă custom este `boards/olimex_rp2350pc.h` (rezolvat prin
`PICO_BOARD_HEADER_DIRS`, convenția pico-sdk 2.3.1).

## DATELE JOCULUI / GAME DATA — NU sunt incluse (©)

**Română:** acest repo conține DOAR firmware și documentație. Datele jocului
(`prince.zip` — sprite-uri, muzică, midis) sunt **protejate de copyright și NU
sunt incluse**. Descarcă-le din repo-ul original
[github.com/rh1tech/frank-prince](https://github.com/rh1tech/frank-prince)
(sau [frank.rh1.tech](https://frank.rh1.tech/)), dezarhivează și copiază
folderul `prince` pe cardul SD formatat FAT32.

**English:** this repository contains firmware and documentation only. The
game data (`prince.zip` — sprites, music, midis) is **copyrighted and NOT
included**. Download it from the original repository
[github.com/rh1tech/frank-prince](https://github.com/rh1tech/frank-prince)
(or [frank.rh1.tech](https://frank.rh1.tech/)), unzip, and copy the `prince`
folder to a FAT32-formatted SD card.

## Flash

1. Ține apăsat **BOOT1** + apasă **RESET** → apare unitatea **RPI-RP2**.
2. Copiază `frank-prince.uf2` (din `build-olimex/`, sau din Release).
3. Placa repornește cu firmware-ul. Video: HDMI 640×480@60; audio: jack.

## Credits

- **frank-prince / FRANK (Murmulator)** — Mikhail Matveev (rh1tech)
- **SDLPoP** — David Nagy și contributorii
- **Prince of Persia (1989)** — © Jordan Mechner / Broderbund
- Driver ES8311 — secvență de inițializare portată din **esp-bsp** (Espressif,
  Apache-2.0), pe scheletul driverului de audio din murmheretic

Licențe: vezi `LICENSE` și sursele respective. Portarea Olimex se respectă
aceleași licențe ca proiectul de bază.
