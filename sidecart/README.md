# DOOM Accelerator

Microfirmware for the [SidecarTridge Multi-device](https://sidecartridge.com)
by [Neil Rackett](https://x.com/neilrackett)

## Introduction

**Download [MD/DOOM](https://neilrackett.com/atarist/md-doom) instead**

At the heart of your SidecarTridge Multi-device is an RP2040 processor with the same raw power as a 386DX or 486SX CPU.

The DOOM Accelerator microfirmware is designed to harness that power to offload processor heavy processes like C2P to free up the main CPU.

The ST host side lives in [`../src`](../src) (`sidecart_md.c/.h`, `sidecart_stubs.S`);
this directory is the RP2040 firmware.

## Installation

1. Download the latest files from the [releases page](https://github.com/neilrackett/ataritt-doom/releases).
2. Copy the `.uf2` and `.json` files to the `/apps` folder of your SidecarT's microSD card.
3. On the Booster screen, press ESC for the app list and select the DOOM Accelerator app.
4. To return to Booster, turn on your ST while holding the SELECT button on your SidecarT.

You'll know if the microfirmware is working because you'll see "DOOM Accelerator ready" on your ST's screen as it boots.

## How it works

With a [SidecarTridge Multi-device](https://sidecartridge.com) running the [DOOM Accelerator](sidecart/README.md) firmware, the chunky-to-planar conversion and all 16-colour reduction are offloaded to the RP2040, and the look is selectable at runtime from the numeric keypad (your choice persists in `doomrc.cfg`):

- **Keypad `*`** — cycle the **dither mode**.
- **Keypad `/`** — cycle the **palette** (the 16 on-screen colours).

When the view is shrunk (smaller window), the RP2040 scales it up to fill the viewport with an even border — so smaller views look full-size _and_ run faster, because fewer pixels are uploaded each frame.

| Keypad `*` — dither | Description                           |
| ------------------- | ------------------------------------- |
| Nearest             | Closest palette colour, no dithering  |
| 2×2 Bayer           | Ordered dither, 2×2 matrix            |
| 4×4 Bayer           | Ordered dither, 4×4 matrix            |
| Halftone            | 4×4 clustered-dot ("newspaper print") |

| Keypad `/` — palette | Description                                |
| -------------------- | ------------------------------------------ |
| Fixed                | Hand-tuned subset of the DOOM palette      |
| Generated            | Median-cut + k-means from the live palette |
| EGA                  | IBM EGA 16 colours                         |
| C64                  | Commodore 64                               |
| Spectrum             | ZX Spectrum                                |
| PICO-8               | PICO-8 fantasy console                     |
| Greyscale            | 16-step grey ramp                          |

Each dither mode combines with any palette.

## Building

The firmware builds against the Raspberry Pi Pico SDK + Pico Extras, registered
as git submodules of the `ataritt-doom` repo at `sidecart/pico-sdk` and
`sidecart/pico-extras`.

From the repo root:

```sh
git submodule update --init --recursive sidecart/pico-sdk sidecart/pico-extras
```

(`rp/build.sh` also runs this automatically and pins the SDK tags.)

You also need:

- the ARM cross-toolchain, CMake, and picotool on your PATH.
- [`atarist-toolkit-docker`](https://github.com/sidecartridge/atarist-toolkit-docker) to compile the ST host side.

## Build

In this folder, run:

```sh
make build        # release build
make build debug  # debug build and test apps
```

Flash the resulting `.uf2` to the SidecarT or copy both the `.uf2` (with the version
number removed) and `.json` to the `apps` folder on your SidecarT's SD card.

## Layout

```
rp/src/                RP2040 firmware
  stdoom_commands.h    command IDs + shared ROM-in-RAM memory map
  stdoom_protocol.c    cartridge-bus protocol DMA bridge
  stdoom_worker.c      command dispatch (PING/INIT/SET_MAP/BLIT_ROWS/C2P) + token handshake
  emul.c               bootstrap (ROM emulation + worker loop)
target/atarist/src/    m68k cartridge stub (main.s) → target_firmware.h
desc/app.json          SidecarT app manifest
```
