# TTDOOM / STDOOM MD — project context for agents

## What this is

STDOOM is a port of Linux Doom to the Atari ST. From one source tree this repo
builds **TTDOOM** (TT and Falcon, 320x240 in 256 colours) and **STDOOM** with a
**SidecarTridge Multi-device accelerator** path: when a SidecarTridge is
present, the RP2040 handles the CPU-intensive chunky-to-planar (C2P)
conversion, freeing the ST's 68000 for game logic.

Build targets:

- **`src/`** — the game sources, built by the root `Makefile` (the only one for
  the game): `make` builds `dist/STDOOM.TOS` (ST, with the accelerator client)
  and `dist/TTDOOM.TOS` (TT/Falcon, 320x240x256, 68030, `-DTTDOOM
  -DSCREENHEIGHT=240`, no sidecart objects; see "TTDOOM" below). Objects go in
  `build/`, programs in `dist/` (which also holds the WADs; `make clean` leaves
  them). Run on the host, the Makefile runs the build in atarist-toolkit-docker
  via `stcmd`.
- **`sidecart/rp/`** — the RP2040 firmware (`.uf2`, built with `bash sidecart/rp/build.sh pico_w debug`)
- **`sidecart/tests/`** — standalone SidecarTridge test apps:
  - `make -C sidecart tests` to build all test apps at once
  - `make -C sidecart pingtest` for detection + transport smoke tests
  - `make -C sidecart c2ptest` for the full 320x200 C2P round trip and screen copy
  - `make -C sidecart recttest` for the dirty-rect (arbitrary `x,y,w,h`) pipeline
  Use these first when validating new SidecarTridge features before wiring them into STDOOM

@sidecart/README.md

## Milestone plan

| #   | What                                                                                                                                 | Status                      |
| --- | ------------------------------------------------------------------------------------------------------------------------------------ | --------------------------- |
| 1   | **Detection** — ST PINGs the SidecarTridge; firmware echoes token + version string; ST reports detected/not-detected at startup      | **Complete**                |
| 2   | **C2P offload** — ST uploads chunky framebuffer; RP2040 converts to planar and writes to shared ROM-in-RAM slot; ST blits from there | **Complete**                |
| 3   | **Full C2P replacement** — route *all* rendering (splash, menus, intermission, automap, status bar, gameplay) through the accelerator when present, with dirty-rect support (status-bar cells, zoomed view); refactor into a separate `sidecart_c2p.c` | **Complete**                |
| 4   | **Palette + dither modes** — `SET_MAP` replaced by `SET_PALETTE` (raw 768-byte RGB); RP2040 owns all colour reduction and returns chosen 16 ST colours; selectable modes: nearest-colour, greyscale, 2×2/4×4 Bayer; median-cut + k-means palette generation; palette selection quality needs future refinement | **Complete**                |
| 5   | **Async data processing** — non-blocking C2P dispatch; ST returns to game logic immediately after firing C2P and synchronises only at vsync before the planar copy | **Abandoned** (upload-bound, ~1.2fps; saved on `turbo-async`) |
| 6   | **Upload-optimisation investigation + new C2P modes** — standalone `UPTEST.TOS` upload benchmark, HW-confirmed: ~203ms/frame upload floor (≈4.9fps ceiling), RLE ~10× slower (68000 encode ~1.8s/frame), dirty-row a net loss ⇒ upload irreducible for gameplay; two new greyscale-dither modes (GREY 2×2/4×4 Bayer) + mode-switch key moved to keypad `*`; `-noturbo`→`-nosidecart` | **Complete** (HW-confirmed)  |
| 7   | **Viewport scaling + palette/render expansion** — RP2040 nearest-neighbour upscales any shrunk gameplay view into the size-9 framed layout (even GRNROCK border, HUD message in the top border) at no extra upload cost, so smaller views run faster (the one lever that beats software); render axis (keypad `*`) = NEAREST/2×2/4×4 Bayer/HALFTONE; palette axis (keypad `/`) = SUBSET/GENERATED/EGA/C64/Spectrum/PICO-8/GREYSCALE (greyscale demoted from render mode to palette); `s_subset` re-synced to `atari_c2p.c`; boot banner shows on release builds | **Complete** (HW-confirmed)  |

### Approach

- Simplest thing that works first, then evolve. The accelerator is
  synchronous with a single planar slot, and keeps md-js's token map
  (`$FAF000`/`$FAF004`), so `src/sidecart_stubs.S` is md-js's file unchanged.
- Accelerated output is not meant to be pixel-identical to the software C2P
  (its dithering differs).
- Longer term, the accelerator design is the model for a clean Atari ST SDL
  XBIOS driver (atarist-sdl's `SDL_xbios_md.c` is experimental and unstable).
  That is why `atari_c2p.c` (software) and `sidecart_c2p.c` (accelerated) are
  split the way an SDL driver swaps implementations.

## Milestone 1 — complete

The detection protocol (`sidecart_md_detect` in `src/sidecart_md.c`) is complete:

1. Checks ready magic at `$FAF00A` (expect `$53`)
2. Checks the seed at `$FAF004` to guard against a dead cartridge
3. Sends `CMD_STDOOM_PING` (op `$0010`, d3=`'STDM'`) via `stdoom_send_sync_command`
4. Waits for the RP2040 to echo the random token back to `$FAF000`

On success the firmware writes `DOOM Accelerator/1.0` into the shared result
buffer and `sidecart_md_result()` returns it to the ST side.

Use `make -C sidecart pingtest` and `sidecart/tests/PINGTEST.TOS` to validate detection and
transport in isolation, and `make -C sidecart c2ptest` / `sidecart/tests/C2PTEST.TOS` for
the full visible round trip, before wiring new SidecarTridge features into
`src/`.

## Milestone 2 — complete

C2P offload works end-to-end. STDOOM proper renders gameplay frames through the
accelerator on a Mega STE at **16 MHz + cache**, with correct geometry.

- `sidecart/tests/C2PTEST.TOS` runs the full synchronous round trip: detect,
  `INIT`, `SET_MAP`, `BLIT_ROWS`, `C2P`, then planar copy to the ST screen. It
  prints per-phase timings and a planar checksum, and is the preferred benchmark
  and regression tool.
- STDOOM's `c2p_screen_md` (in `atari_c2p.c`) uploads `screens[0]` in 6-row
  chunks, triggers `C2P`, and copies planar slot 0 to the screen, with a clean
  fall-through to software `c2p_screen_lorez` on any failure or non-gameplay
  frame.
- STE blitter copy is used for the planar-to-screen copy on STE/Mega STE, with a
  CPU longword fallback on plain ST.

### Hard-won fixes that made it work (do not regress)

- **Mega STE cache register (`$FFFF8E21`)** uses canonical MSTE_CC values
  `$FF`/`$FE`/`$F4` (cache = bit0). Naive `$01`/`$03` writes corrupt the
  must-be-set upper bits and destabilise the machine. Kept in sync between
  `atari_checkcpu.c` and `sidecart_md.c`'s cache guard.
- **STE blitter registers start at `$FFFF8A20`** (the first 16 words at
  `$8A00` are halftone RAM). The old `$8A02..$8A1E` offsets wrote into halftone
  RAM, so the blitter never ran and the screen stayed black. Fixed in
  `atari_c2p.c` and `c2ptest.c`.
- **`Super()` must be mode-aware.** STDOOM runs the whole game loop in
  supervisor mode (`I_Init` enters super and stays), so the cache guard uses
  SUP_INQUIRE and only toggles mode when entered from user mode. The naive
  `Super(0L)/Super(ssp)` pattern crashes (2 bombs) when already supervisor.
- **The RP2040 must NOT byte-swap the planar output.** `stdoom_pack_to_planar`
  `memcpy`s the plane words to ROM-in-RAM; the ST reads uint16 values directly.
  Byte-swapping exchanges the two 8-pixel halves of every 16-pixel group and
  scrambles columns.
- **C2PTEST's synthetic pattern hides pixel-order bugs** (adjacent pixels map to
  the same ST colour). Validate C2P pixel ORDER against real Doom output or a
  per-pixel-varying pattern, not the smooth gradient.

### Cache guard note

Mega STE cache is temporarily disabled around SidecarTridge command traffic in
`sidecart_md.c`, then restored afterwards, because cache can hide the ROM3 bus
reads the protocol relies on.

### Key files

| File                                        | Purpose                                           |
| ------------------------------------------- | ------------------------------------------------- |
| `src/sidecart_md.c/.h`           | ST-side client: detect/PING + INIT/SET_MAP/BLIT_ROWS/C2P(rect), mode-aware cache guard, per-command IPL-7 interrupt mask |
| `src/sidecart_stubs.S`           | Low-level SidecarTridge bus protocol (asm)        |
| `src/sidecart_c2p.c/.h`          | **(M3)** all accelerated C2P: reduction map, `c2p_screen_md()`/`c2p_statusbar_md()` drawfuncs, blitter/CPU planar→screen rect copy, palette hook; `sidecart_c2p_install()` overrides the software pointers |
| `src/atari_c2p.c`                | Pure software C2P + the function pointers `sidecart_c2p.c` installs over (no sidecart code since M3) |
| `src/atari_checkcpu.c`           | Mega STE turbo/cache register (canonical `$FF`/`$FE`/`$F4`) |
| `sidecart/rp/src/stdoom_worker.c`           | RP2040 worker: PING/INIT/SET_MAP/BLIT_ROWS/C2P dispatch, `pack_to_planar_rect`, token echo |
| `sidecart/rp/src/include/stdoom_commands.h` | Shared memory map, command IDs, offsets           |
| `sidecart/tests/pingtest.c`                 | Standalone detection + transport smoke test       |
| `sidecart/tests/c2ptest.c`                  | Standalone full C2P round-trip test (`make -C sidecart c2ptest`) |
| `sidecart/tests/recttest.c`                 | Standalone dirty-rect (`x,y,w,h`) pipeline test (`make -C sidecart recttest`) |

### Diagnostic output (startup, before game loads)

```
MD detected: DOOM Accelerator/1.0   (accelerator present)
MD not detected; SW C2P             (no accelerator → software path)
MD C2P init failed; SW C2P          (detected but INIT/SET_MAP failed)
```

The verbose `MD ready=/stage=/irq=` dump used while bringing up M1/M2 was removed
in M3. To restore it for low-level transport debugging, use
`sidecart_md_detect_verbose()` (stage codes: 0=ok, 1=ready mismatch, 2=dead seed,
3=PING timeout) and the `STDOOM_DBG_*` counters declared in `sidecart_md.h`.

## Milestone 3 — complete

Full C2P replacement. With the accelerator present, **every** screen (splash,
menus, intermission/finale, automap, status bar, gameplay) renders through the
accelerator, with dirty-rect support for the shrunk gameplay view and the status
bar. Confirmed on hardware (Mega STE, 16 MHz + cache).

- **Architecture split.** `atari_c2p.c` is pure software again and knows nothing
  about the sidecart. All accelerated code lives in `sidecart_c2p.c`;
  `sidecart_c2p_install()` (called after `init_c2p_table()` in `I_InitGraphics`)
  overrides the software `c2p_screen_drawfunc`/`c2p_statusbar_drawfunc`/
  `set_doom_palette` pointers. `sidecart_c2p_init()` (in `D_DoomMain`, user mode)
  runs detection + `INIT`/`SET_MAP`. This mirrors how a clean SDL XBIOS driver
  swaps implementations.
- **Rect pipeline.** `CMD_STDOOM_C2P` carries an optional convert rect
  `(x,y,w,h)`; the firmware `stdoom_pack_to_planar_rect` converts only that
  sub-rect. `c2p_statusbar_md` updates only the dirty status-bar cells; the
  shrunk gameplay view uploads/converts/copies only the active view rectangle.

### Hard-won fixes that made it work (do not regress)

- **Blitter from ROM4 is unreliable for sub-rects.** A multi-row blit
  (`X_COUNT=80, Y_COUNT=h`) tears on the last rows; a blit with `INC_Y!=0` (true
  sub-rect) produces stripe corruption. `sidecart_md_copy_planar_to_screen` uses
  a linear blit (`X_COUNT=total, Y_COUNT=1`) only for full-width rects, and a CPU
  row-by-row copy for true sub-rects.
- **Inverted/empty dirty box.** The software `c2p_statusbar` wrapper forces
  `y_begin` up to `SCREENHEIGHT-32`; a top-screen HUD message then yields
  `y_begin > y_end`. `c2p_statusbar_md` must reject `y_end <= y_begin ||
  x_end <= x_begin` first, or the unsigned width/height underflows and the copy
  runs off screen RAM (2-bomb address-error crash on "start new game").
- **Shrunk-view border.** Doom draws the GRNROCK border into `screens[0]` only on
  the few frames after a view-size change or menu/automap dismissal (its
  `borderdrawcount=3`). `c2p_screen_md` forces a full-frame push for 3 ticks
  whenever the border may have been redrawn, else the partial path leaves stale
  menu pixels around the view.
- **Interrupt masking.** The bus protocol's ROM3 reads must be atomic; an MFP
  keyboard/timer interrupt mid-command corrupts it and bounces the frame to the
  software renderer (a dithered flash on menu keypresses). Each per-frame command
  raises the 68000 IPL to 7 via `sidecart_md_set_intr_mask(1)` (supervisor only;
  user-mode init runs unmasked since MOVE-to-SR is privileged).

Validate the dirty-rect pipeline in isolation with
`make -C sidecart recttest` / `sidecart/tests/RECTTEST.TOS` before touching STDOOM.

## Milestone 4 — palette pipeline

- On `I_SetPalette`, `CMD_STDOOM_SET_PALETTE` sends the 768-byte DOOM palette.
  The RP2040 returns 16 ST colours at `STDOOM_STCOLORS` (`$FAF020`), which the
  host writes to the palette registers. Each frame is `BLIT_ROWS` (chunky
  pixels) then `CMD_STDOOM_C2P`.
- The generated palette is median-cut plus 8 Lloyd (k-means) passes with
  redmean distance; a hand-tuned fixed subset is the alternative.
- Do not regress: `stdoom_displayed_channel` must use `(v >> 4) * 17`. The STE
  keeps its extra colour bit at nibble bit 3, so multiplying the raw nibble
  scrambles intensities (an all-purple generated palette).
- Do not regress: RP2040 SRAM is tight. Don't reintroduce the 32 KB
  `s_planar_words` staging buffer; `stdoom_pack_to_planar_rect` packs straight
  into `s_planar_mem`. Re-adding it panics with out-of-memory in
  `settings_init`.
- Open: palette quality. Candidates are the median-cut axis weighting
  (currently R×2, G×4, B×3), more or better-seeded k-means passes, and
  regenerating cleanly for the damage/radiation palettes.

## Milestone 5 — async dispatch (abandoned)

Built and tested on hardware on the `turbo-async` branch (not merged). Firing
the C2P and synchronising at vsync worked, but the accelerator is upload-bound,
not conversion-bound, so hiding the conversion gave ~1.2 fps against ~4 fps in
software.

## Milestone 6 — upload limits (measured on a Mega STE, 16 MHz + cache)

- Benchmark: `UPTEST.TOS` (`make -C sidecart uptest`), with real frames from
  `sidecart/tests/png2chunky.py` (generates `doom_frames.h`, gitignored).
- A 64000-byte frame in 6-row chunks takes ~203 ms (307 KB/s, 34 commands): a
  ~4.9 fps ceiling before any rendering. 3 rows take 223 ms, 2 rows 307 ms and
  1 row 416 ms; each command costs ~1.28 ms. Bigger chunks need a protocol cap
  raise and save at most ~20%.
- PackBits RLE compresses to 84–95%, but encoding on the 68000 takes ~1.8 s a
  frame: ~10× slower overall.
- Dirty-row deltas don't help: ~100% of rows change between frames, and the
  compare costs 66–83 ms.
- Pre-reducing to 4bpp on the ST was rejected: it moves colour reduction back
  to the 68000 and loses RP2040 post-processing such as dithering.

## Milestone 7 — viewport scaling

- `stdoom_pack_to_planar_upscale()` (`CMD_STDOOM_C2P` with the magnify flag in
  the spare low nibble of `rx`) nearest-neighbour upscales the small view
  `(viewwindowx, viewwindowy, scaledviewwidth, viewheight)` into the size-9
  framed rect `STDOOM_FRAME_VIEW_*` = `(16,10,288,148)` of planar slot 0.
- `c2p_screen_md`: `scaledviewwidth < 288` upscales into the frame, sizes 9 and
  10 convert 1:1, and full-screen or non-gameplay frames go full-frame.
- Do not regress: the upscale writes only the framed view rect. The GRNROCK
  border, top message strip and status bar persist from other writes. Don't
  make it fill the whole play area (the borderless variant was rejected). The
  framed copy is a sub-rect, so it uses the CPU copy on an STE; full-width
  copies use the linear blit.
- Keep in sync: `STDOOM_MODE_*`/`COUNT`, `STDOOM_PALGEN_*`/`COUNT` and
  `STDOOM_FRAME_VIEW_*` between `sidecart/rp/src/include/stdoom_commands.h` and
  `src/sidecart_md.h`; `s_subset[]` (`stdoom_worker.c`) and `subset_lorez[]`
  (`src/atari_c2p.c`).
- Keypad `*` picks the dither (nearest, 2×2 or 4×4 Bayer, halftone) and keypad
  `/` the palette (subset, generated, EGA, C64, ZX Spectrum, PICO-8,
  greyscale). Both persist in `doomrc.cfg`. Greyscale is a palette, not a
  dither mode: the redmean distance is close enough to luma.

## Boot banner

"DOOM Accelerator ready" shows only with a release firmware. A debug build's
`stdio_init_all()` and DPRINTFs run before `emul_publish_rom()` and lose the
cold-boot race to TOS's cartridge scan. The string lives in
`sidecart/target/atarist/src/main.s` (regenerate `target_firmware.h` after
changing it). STDOOM's own `MD detected:` line is the fallback.

## Build commands

```bash
# All commands run from repo root

# Standalone test apps
make -C sidecart tests      # builds all test apps (ping, c2p, rect)
make -C sidecart pingtest   # builds sidecart/tests/PINGTEST.TOS
make -C sidecart c2ptest    # builds sidecart/tests/C2PTEST.TOS
make -C sidecart recttest   # builds sidecart/tests/RECTTEST.TOS

# The game (root Makefile; runs in atarist-toolkit-docker via stcmd)
make                  # dist/STDOOM.TOS + dist/TTDOOM.TOS
make stdoom           # dist/STDOOM.TOS
make ttdoom           # dist/TTDOOM.TOS
make clean            # build/ and those programs (the WADs in dist/ stay)

# RP2040 microfirmware for SidecarTridge Multi-device
make sidecart debug   # builds both test apps plus sidecart/dist/[UUID]-[VERSION].uf2
```

## Release

`.github/workflows/release.yml`: every push to `main` (except prose,
`docs/` and `sidecart/` only) builds `dist/TTDOOM.TOS` and
`dist/STDOOM.TOS` with `stcmd make ttdoom stdoom`, moves the lightweight
`latest` tag onto the commit through the API, and replaces those two on the
hand-written `latest` release with `gh release upload --clobber`: no title
or notes change, and no other asset is added or touched. It checks out only the `src/atarist-sfp004` and
`lib/xpad` submodules, not the Pico SDK. No version tags yet.

## Xpad (gamepads)

Anything that reads or publishes Xpad: read
`lib/xpad/.claude/skills/xpad/SKILL.md` first.

- `lib/xpad` is the atarist-xpad submodule, pinned to a release tag; only
  its consumer half (`xpad.c`) is built, into both programs. The README
  carries the licence line, linking to the licence at the pinned tag: keep
  that link's tag in step when the submodule moves.
- `src/i_xpad.c` finds the provider in `I_Init` (user mode, before
  `Super`; `-noxpad` skips it) and polls the first connected pad from
  `I_StartTic`. Button changes become key events through Doom's own
  bindings (`key_fire` etc.), the reverse of Xpad's keyboard driver; in the
  menus the d-pad is the arrows, south Enter (`y` to a question), east
  Backspace (`n`). A button releases the key it pressed even if the menu
  changed meanwhile, and a button held across a menu change presses
  nothing. `I_XpadMove` (called from `G_BuildTiccmd`) adds analogue
  movement; in play the d-pad bits a provider folds from a deflected left
  stick are ignored, so the stick strafes rather than turns.
- STDOOM reads the IKBD itself (the `$118` handler), so providers' joyvec /
  mouse injection, XPADEMU and the joystick/keyboard example drivers can't
  reach it; joystick 1 packets (`$FF`) go to `I_XpadJoystick` and merge in
  as a one-button pad.
- Doom doesn't run `I_StartTic` during a screen melt, so a pad tap shorter
  than a melt is missed (keyboard presses are buffered and aren't).
- Testing: a throwaway provider that publishes one analogue pad and prints
  its buffers' addresses, then Hatari's `w l <addr> <buttons>` (and `w` for
  the stick bytes at +4..+7) through the command FIFO drives it.

## Shared memory map (ROM4 base `$FA0000`)

| Offset  | ST address | Size    | Purpose                                         |
| ------- | ---------- | ------- | ----------------------------------------------- |
| `$0000` | `$FA0000`  | 32000 B | Planar slot 0 (Milestone 2)                     |
| `$F000` | `$FAF000`  | 4 B     | Random token (RP2040 writes to unblock ST)      |
| `$F004` | `$FAF004`  | 4 B     | Random token seed                               |
| `$F008` | `$FAF008`  | 2 B     | Status (`$00`=idle, `$01`=busy, `$02`=done)     |
| `$F00A` | `$FAF00A`  | 2 B     | Ready word (both bytes = `$53` when worker up)  |
| `$F100` | `$FAF100`  | 2048 B  | Result buffer (PING writes version string here) |

## TTDOOM (TT / Falcon) — notes

- **Build:** the `ttdoom` target in the root `Makefile` (`build/*.tt.o`,
  `-m68020-60 -msoft-float -fomit-frame-pointer -DTTDOOM -DSCREENHEIGHT=240`;
  no `-mtune=68030`, whose 64-bit multiplies for constant divides a 68060
  traps, and which timed no faster on a 68030). The
  sidecart objects are left out; `sidecart_c2p.h` stubs the API under `TTDOOM`
  and `m_misc.c` keeps `render_mode`/`palette_gen` so a shared `doomrc.cfg`
  round-trips.
- **Mode:** `atari_vmode.c` (after atarist-stdl's `responsive.c`): TT Low via
  `Setscreen(..,7)` (C2P writes each line to two scanlines); Falcon
  `VsetScreen(..,3,BPS8|COL40|VGA|VERTFLAG)` on VGA, or `BPS8|COL40|PAL|OVERSCAN`
  (384 wide, picture 32 in) on RGB/TV. Restores the desktop mode + palette.
  The palette is blacked out after the mode change (the desktop's colour 0
  would show the cleared screen white until Doom's first frame), and the
  C2P's palette grid diagnostic is drawn only with `-devparm`.
- **Memory (4 MB):** `I_ZoneBase` (TTDOOM) reserves the ST-RAM display
  pages first (`atari_vmode_reserve`) and then caps the zone at what is free
  less `ZONE_SPARE` (128K for the WAD directory and stdio); with TT-RAM or
  plenty of RAM it is the full 2.5 MB. A zone of ~2.1 MB runs the shareware
  E1 tour and timedemo once the sounds missing from the WAD share one
  pistol copy (`I_InitSound`: they used to cost 53 copies, 300K, for good).
  Measured under Hatari on 4 MB TT (ST-RAM only) and Falcon (VGA, RGB).
- **240 lines, DOS proportions:** `SCREENHEIGHT` is compile-time. 320x200
  was shown on 4:3 (DOS, ST), so TTDOOM's square-pixel 240 lines stand for
  those 200: 320x200 art (pages, menus, WI, finale) is drawn with `v_page` set
  and stretched (screen row d = page row d*200/240, `v_video.c`), and the 3D
  view's vertical scale is `YASPECT()` (x6/5, `doomdef.h`) of the horizontal -
  walls, plane row distances, sprites, weapon, sky - with lighting on the
  unstretched scale (`LIGHT_INDEX` in `r_segs.c` divides the wall scale
  back down, exact for the scales Doom produces). `BASEYCENTER` and
  `skytexturemid` are id's 100 again.
  Status-bar Y positions use `ST_YOFS()`. At 200 all of this reduces to the
  original code (ST renderer verified identical by disassembly).
- **Double buffering:** `atari_vmode.c` allocates two ST-RAM pages;
  `I_FinishUpdate` draws into `atari_vmode_draw_page()` and flips with
  `atari_vmode_show_page()` (writes `$FF8201/03`, latched at the next frame).
  Drawing into a page waits until 2 VBLs (`_frclock`) after the flip that hid
  it. The status bar's dirty box is the union of this frame's and the last
  (each page last took the bar two frames ago).
- **C2P:** a merge C2P (`C2P8_MERGE_FN` in `atari_c2p.c`): five register
  exchange passes per 16 pixels and long writes, instantiated per scanline
  stride (320/384) and copy count (lines doubled on TT, zoom repeats), so a
  zoomed line is converted once. ~2.5x (TT) / ~4x (Falcon) faster than the
  old `movep` version under Hatari; bit-identical output.
- **Span loop:** `R_DrawSpan`/`R_DrawSpanLow` have a hand-written loop for
  68020+ (`__M68020__`, the ISA macro; a `-mtune` changes which `__mc680x0__`
  are defined): 10.5 instructions a pixel vs gcc's 14, bit-identical
  (checked span by span against the C loop over a timedemo). The 68000 build
  keeps the C loop; its `R_DrawColumn`/`R_DrawSpan` machine code is unchanged.
- **FixedMul:** on a 68020 to 68040 (`_CPU` cookie, `M_InitFixed` in
  `main`) it is one 64-bit `muls.l`, bit-identical to the 68000's split
  multiply (checked for 50 million products on the host and a million on
  an emulated 68030). The 68060 lacks the 64-bit form and keeps the split.
  TTDOOM is also built with `-fomit-frame-pointer`. Together +6-8% under
  Hatari.
- **Low detail (F5):** works in both binaries. `R_DrawColumnLow` shares
  `R_DrawColumn`'s asm (each pixel written twice); id's versions doubled the
  global `dc_x` (breaking the sprite loop) and `R_DrawSpanLow` counted after
  doubling (drew twice the span). Fuzz/translated columns have `Low`
  versions. Width checks outside the renderer use `scaledviewwidth`.
- **Word colormaps (TTDOOM, `WORD_COLORMAPS`):** on a 68020+ the low-detail
  column and span loops store each pixel pair with one `move.w` from
  `colormaps_w` (`R_InitColormaps`: every light table with each colour in
  both bytes, 17K), found from the byte table's offset in `colormaps`.
  Bit-identical: every low column and span of a timedemo checked against
  the high-detail loop. +3-4% in low detail under Hatari. The 68000 keeps
  its byte stores (it has no scaled index).
- **Shrunk views (sizes 6-9, TTDOOM):** `c2p_view_8bpp` converts the view
  rect and what `playdirtybox` says changed, not the static border. Anything
  drawn above the status bar outside the view must reach `V_MarkRect`: the
  HUD message's erase (`HUlib_eraseTextLine`) marks its rows for this.

### Hard-won fixes (do not regress)

- **STDOOM's sound VBL replaces `$70` without chaining**, so TOS's VBL work
  stops. `Setscreen`/`VsetScreen` with a resolution change waits for a VBL and
  hangs: wrap such XBIOS calls in `I_SoundSuspendVbl()`/`I_SoundResumeVbl()`.
  Under `TTDOOM` the sound VBL increments `_frclock` itself, which the page
  flip waits on.
- **Don't union dirty boxes with `M_AddToBox`**: its `else if` misreads an
  empty (cleared) box. `I_FinishUpdate` takes min/max by hand.
- **`D_Display` rewinds `dirtybox`** to its value before the 3D view, HUD,
  border and menu are drawn, so at `I_FinishUpdate` it covers the status bar
  only. Under `TTDOOM`, `V_MarkRect` also feeds `playdirtybox` (everything
  above the status bar), which `I_FinishUpdate` takes and clears each frame.
- **TT GPIP bit 7 (mono detect) is XORed with the DMA-sound play line**, so it
  reads inverted once sound is playing. Read it at startup
  (`atari_vmode_supported()` in `main`, before `I_InitSound`).
- **Accelerator gate:** `sidecart_c2p_init()` skips detection when `_MCH` is
  TT or later or `Getrez() != 0`, so a cart in a TT/Falcon is never touched.

### Testing in Hatari

TOS images live in `tmp/tos/` (gitignored; `tos306uk.img` for TT; use EmuTOS
512k for Falcon). Run headless with `SDL_VIDEODRIVER=dummy`, pass
`--addr24 off` (after `--machine`) whenever TT-RAM is enabled and
`--alert-level fatal`, so no dialogs pop up. `--tos-res ttlow` boots a TT in
TT Low for testing `STDOOM.TOS`'s 8-plane path. Over the command FIFO,
`hatari-event keypress 1` is the "1" key: give single-digit scancodes in hex
(`0x01` is Esc). Frame checksums differ by a few sprite-edge pixels between
any two builds (layout-dependent reads), so compare renderer changes within
one binary, not across builds.

## Reference projects (siblings of this repo)

- `md-js`: the cleanest plain-TOS example of the cartridge transport, and the
  source the firmware and `sidecart_stubs.S` were copied from
  (`rp/src/{mdjs_protocol.c, js_worker.c, emul.c}`,
  `target/atarist/src/{sidecart_stubs.S, mdjs.c, mdjs.h}`).
- `md-sprites-demo/target/atarist/src/main.s`: the validated STE blitter
  copy-to-screen (correct `$FFFF8A20` offsets). Prefer it for blitter
  questions.
- `md-sdl`: experimental, not a source of truth. Useful for structure
  (`rp/src/emul.c`'s `sdl_c2p`, `src/video/xbios/SDL_xbios_md.c`) and the Mega
  STE `$FF`/`$FE`/`$F4` values in `SDL_megaste.c`; verify before relying on
  it.

## Deferred offload ideas (not planned)

- STDOOM: wall and floor drawing on the RP2040 (the biggest possible win, but a
  lot of protocol work), sound mixing into a DMA buffer, the screen melt as a
  planar post-process, palette effects (damage/radiation tints), automap line
  drawing.
- A general SDL driver: C2P for other modes (medium/high res, STE 4096
  colours), framebuffer scaling or letterboxing, sprite compositing with a
  colour key, glyph stamping, 2D primitives, audio mixing, LZ4 asset
  decompression, YUV/RGB conversion for video.
- The RP2040 has ~264 KB SRAM and two cores (Core 1 could work while Core 0
  runs the protocol); SRAM limits how many buffers can coexist.
