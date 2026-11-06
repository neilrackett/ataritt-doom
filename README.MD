# TTDOOM

<img src="docs/titlepic.png" alt="TTDOOM title screen" width="320" />
<img src="docs/screen1.png" alt="TTDOOM screenshot" width="320" />

[STDOOM](https://github.com/indyjo/STDOOM) expanded and accelerated for Atari TT
and Falcon, by [Neil Rackett](https://neilrackett.com/atarist)

## Introduction

Welcome to TTDOOM: full screen, full colour DOOM on the Atari TT or Falcon, thanks to an optimised 68030 rendering pipeline that enables it to run at close to original speed at 320x240 in 256 colours.

## System requirements

- Atari TT or Falcon with at least 2MB RAM
- 5MB hard drive space

## Installation

- Download `TTDOOM.TOS` and `DOOM1.WAD` from the [releases page](https://neilrackett.com/atarist/ataritt-doom).
- Copy them to your hard drive.
- Run `TTDOOM.TOS`.

## Features

- 320x240 in 256 colours
- Zoomed mode for higher performance (activate by reducing window size using = key or menu)
- DMA sound effects
- YM-2149 music
- Network gameplay not supported

## Controls

| Action                | Keyboard       | Mouse                 | Joystick       | Gamepad                    |
| --------------------- | -------------- | --------------------- | -------------- | -------------------------- |
| Move forward and back | Up and down    | Move forward and back | Up and down    | D-pad or left stick        |
| Turn                  | Left and right | Move left and right   | Left and right | D-pad or right stick       |
| Strafe left and right | `,` and `.`    | Right button and move |                | Shoulders or left stick    |
| Strafe while held     | Alt            | Right button          |                | West (X)                   |
| Fire                  | Control        | Left button           | Fire           | South (A) or right trigger |
| Use, open             | Space          |                       |                | East (B)                   |
| Run                   | Shift          |                       |                | North (Y) or left trigger  |
| Weapons               | `1` to `7`     |                       |                |                            |
| Automap               | Tab            |                       |                | Select                     |
| Menu                  | Esc            |                       |                | Start                      |

The joystick goes in port 1. Gamepads work through any
[Xpad](https://github.com/neilrackett/atarist-xpad) provider, such as COMpad,
MD/Sidepad or a Jaguar pad in a Falcon's enhanced port; the left stick strafes
on an analogue pad. In the menus the d-pad moves, south selects and east goes
back. Pad buttons follow your key bindings, so redefining a key in
`doomrc.cfg` moves its button too. Start TTDOOM with `-noxpad` to ignore any
provider.

## Compiling

The easiest way to compile TTDOOM is to use [`atarist-toolkit-docker`](https://github.com/sidecartridge/atarist-toolkit-docker).

```bash
git submodule update --init src/atarist-sfp004 lib/xpad
stcmd make
```

## Benchmark

- Rename `TTDOOM.TOS` to `TTDOOM.TTP`
- On start, enter parameters: `-timedemo demo3 -nosound`
- The demo plays. When it's finished, a message says:
  Error: timed 2134 gametics in x realtics
- x is the time elapsed in 1/35th of a second. Calculate FPS: 35\*2134/x

## Atari ST

This project also includes some of my abandoned STDOOM experiments, with various optimisations to nudge it closer to being playable on an ST, including:

- [DOOM Accelerator](sidecart/README.md) for [SidecarTridge Multi-device](https://sidecartridge.com) to handle C2P
- Auto switch to 16MHz+cache on Atari Mega STE
- Support for SFP-004 memory mapped FPU if you've installed a 68881 or 68882 FPU
- Fixed low detail graphics mode

They help a little, but If you really want to run DOOM on an ST, download [MD/DOOM](https://neilrackett.com/atarist/md-doom) for your SidecarT instead.

## Licences

Controller input via Xpad, Copyright (c) 2026 Neil Rackett,
BSD-2-Clause. See [its licence](https://github.com/neilrackett/atarist-xpad/blob/v1.1.12/LICENSE).
