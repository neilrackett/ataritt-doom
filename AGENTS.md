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
make ttdoom TOOLCHAIN=mintelf  # dist/mintelf/TTDOOM.TOS, built with gcc 13
make ttdoom TOOLCHAIN=mintelf LTO=1  # dist/mintelf-lto/TTDOOM.TOS
tools/bench/pgo.sh LABEL       # gcc 13 + LTO + PGO, tmp/bench/bin/LABEL.TOS
tools/bench/pgo.sh --dist      # the same from this tree: dist/mintelf-lto-pgo/TTDOOM.TOS
make clean            # build/ and those programs (the WADs in dist/ stay)

# TTDOOM benchmarks (Hatari, see "Testing in Hatari"): baseline vs working tree
tools/bench/mkbuild.sh base @HEAD && tools/bench/mkbuild.sh cur
tools/bench/run.sh base low; tools/bench/run.sh cur low
tools/bench/fuzz.sh base && tools/bench/fuzz.sh cur base   # game logic the same?
RCHECK=1 tools/bench/mkbuild.sh rc && tools/bench/rcheck.sh rc  # pixels the same?

# RP2040 microfirmware for SidecarTridge Multi-device
make sidecart debug   # builds both test apps plus sidecart/dist/[UUID]-[VERSION].uf2
```

## Release

`.github/workflows/release.yml`: every push to `main` (except prose,
`docs/` and `sidecart/` only) builds `dist/STDOOM.TOS` (`stcmd make
stdoom`, gcc 4.6.4) and TTDOOM with gcc 13 and LTO (`stcmd make ttdoom
TOOLCHAIN=mintelf LTO=1`, `dist/mintelf-lto/TTDOOM.TOS`; no PGO, whose
training needs Hatari, a TOS image and the WAD), moves the lightweight
`latest` tag onto the commit through the API, and replaces those two on the
hand-written `latest` release with `gh release upload --clobber`: no title
or notes change, and no other asset is added or touched. It checks out only the `src/atarist-sfp004` and
`lib/xpad` submodules, not the Pico SDK. No version tags yet.

The toolkit image is `neilrackett/atarist-toolkit-docker-x86_64:mintelf`
(`DOCKER_ACCOUNT` and the installer's tag argument), until upstream's has
gcc 13: upstream's published image plus gcc 13 as the toolkit's `mintelf`
branch installs it (`tools/toolkit/Dockerfile`, which says how to rebuild
and push it). Not built from the toolkit's own Dockerfile, which no longer
builds (vasm and vlink have left `sun.hasenbraten.de`). Checked when made:
its gcc 4.6.4 builds of both programs are byte-identical to upstream's
image's, and its gcc 13 + LTO TTDOOM to the arm64 image's.

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

- **Target:** a TT030 with 2 MB ST-RAM and 4 MB TT-RAM. Program (~850K),
  `screens[0..3]` and the 2.44 MB zone all land in TT-RAM there (the
  start-up line says `Fast RAM allocated`); ST-RAM holds the display pages
  and the DMA sound buffers. TTDOOM also runs on a Falcon and a 4 MB TT.
- **Exactness:** the renderer may differ from id's by sub-texel amounts (a
  visual check: `tools/bench/shots.py`); never fewer pixels or features.
  Game logic must stay exact: demos 1-3 must give the same BENCH STATE hash
  (`tools/bench`). C2P and mixer changes are checked bit for bit.
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
- **Box C2P (unzoomed):** `c2p8_box1`/`c2p8_box2` (one or two scanlines a
  row, top-level asm after STDL's SDL2 add-on): a box of rows is one call.
  The 68030 holds one write to ST-RAM while it works on and a second waits,
  so each group's planes are parked in a3-a6 and stored among the next
  group's merges; a row's last group is stored during the next row's
  first. Each loop must fit the 256-byte instruction cache at any
  alignment, at most 240 bytes: the TTDOOM link checks it with `nm`
  (`CACHE_CHECK`). Under Hatari +3-5% on the TT, +2-3% on the Falcon;
  every page of a timedemo identical to the per-row loops' (TT, Falcon VGA
  and RGB, sizes 9 and 10).
- **Pair C2P (low detail):** in low detail the 3D view is in pixel pairs,
  so `c2p8_pairbox1/2` gather a group's 8 distinct pixels into two
  registers (a with c's odd bytes, b with d's), transpose them with four
  exchanges and double each bit by exchanging a register with a copy of
  itself: 56 instructions a group against 74. Used for the view's rows
  outside the dirty box (`c2p8_view_rows`, `c2p_view_8bpp`) when
  `c2p8_pairs()` (low detail, in play, no menu or automap). The
  exchanges' roles were found by simulating the instructions against a
  reference conversion; checked with `-c2ptest` and by decoding pages of
  timedemos (`pagecheck.py`). TT low 25 -> 20 ms (+2 ms for HUD rows).
- **Column loop (68020+, `__M68020__`, the ISA macro; a `-mtune` changes
  which `__mc680x0__` are defined):** vanilla's 16.16 step in four
  instructions a pixel. `pos` keeps the fraction in the high word and the
  7-bit texel row in the low word, which indexes the column directly;
  `addx.l` steps both, the fraction's carry reaching the row through X on
  the next pixel. X starts as the first step's carry (set with `move.w
  ..,ccr`, which also sets Z for an even count) with the fraction a step
  on, so every row is exactly vanilla's `(frac>>16)&127` (the 68000 loop
  keeps 8.8). The row step is taken mod 128 so the low word never carries.
- **Span loop:** after Aki Laukkanen's span for ADoom: u and v in two
  registers with the fractions crossed over (u = v fraction : u texel, v =
  u fraction : v texel*64), so two `addx.l` chain one X flag; `or.w`/`and.w`
  with `$ffc0`/`$f03f` build the index as `$f000 + v*64 + u` (the flat
  + `$1000`). 7.5 instructions a pixel against 10.5, 16 fraction bits
  against 9; a texel wrap carries into the other fraction's lowest bit
  (1/65536 of a texel). Spans 20.5 -> 15 ms low, 36 -> 28 high.
- **Batched walls and flats (`colrec_t`, `spanrec_t`, `r_draw.h`):** the
  seg loop only notes each wall tier (`segtiers`); `R_DrawSegTiers` then
  fetches the columns and sets up column records (`R_SetupColumn`), and
  `R_DrawColumns` draws them. `R_MakeSpans` notes spans (`R_StageSpan`) and
  `R_MapStagedSpans` sets up and draws a visplane's. Each of those loops
  mostly fits the 68030's 256-byte instruction cache, where the seg loop,
  `R_GetColumn` chain and drawer in turn missed a third of the time.
  Sprites, sky and masked segs still draw a column at a time
  (`R_DrawColumn` = set-up + draw of one record).
- **Texture columns:** `R_GetColumn` is inline (`r_data.h`): a cached lump
  or composite is returned directly; `W_CacheLumpNum`/`Z_ChangeTag2` only
  on a miss. That was 3.4 ms a frame of call chains.
- **Fuzz:** asm on a 68020+, five instructions a pixel (seven low), from
  `fuzzoffsetx`, `fuzzoffset` repeated so a column never wraps the table.
  Same pixels as the C loops.
- **FixedMul:** on a 68020 to 68040 (`_CPU` cookie, `M_InitFixed` in
  `main`) it is one 64-bit `muls.l`, bit-identical to the 68000's split
  multiply (checked for 50 million products on the host and a million on
  an emulated 68030). The 68060 lacks the 64-bit form and keeps the split.
  TTDOOM is also built with `-fomit-frame-pointer`. Together +6-8% under
  Hatari.
- **The other fixed-point helpers (`m_fixed.h`, `FIXEDMUL_MULS64`):**
  `FixedDiv` is an inline 64/32 `divs.l` behind the 68000 version's guard.
  It equals the 68000's bit-by-bit division wherever that one works (|a| <
  2^29, |b| < 2^30; 181 million pairs on the host); above that the old
  shifts overflow (and can loop for ever) and `divs.l` agrees with id's
  DOS `idiv`. `FixedScale32` is `FixedMul` (identical for |b| < 65536: all
  callers pass sines, cosines or `FRICTION`; 200 million products), and
  `FixedMulShort` a plain product. `FixedScale` is `FixedMul(a, b<<1)`: the
  68000 version drops a's fraction (`+ neg ? 0xffff : 0` precedence), so
  sprites move by up to a unit. `SlopeDiv` and the byte swaps (`m_swap.h`)
  are inline too. 5.6 ms a frame of calls.
- **Low detail (F5):** works in both binaries. `R_DrawColumnLow` shares
  `R_DrawColumn`'s asm (each pixel written twice); id's versions doubled the
  global `dc_x` (breaking the sprite loop) and `R_DrawSpanLow` counted after
  doubling (drew twice the span). Fuzz/translated columns have `Low`
  versions; `R_DrawVisSprite` takes `transcolfunc` (id's hard-coded
  `R_DrawTranslatedColumn` drew player colours at an undoubled x, which also
  broke the pair C2P's pairs). Width checks outside the renderer use
  `scaledviewwidth`.
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
- **Sound mixer (68020+):** each channel in asm (seven instructions a
  sample, the position stepped by `add.w`/`addx.l`, a sound's last sample
  worked out up front), mixed into `mixacc` in TT-RAM and copied to the
  ST-RAM DMA buffer with long stores: no read-modify-write of ST-RAM per
  channel and sample. Same samples as the C mixer (`-mixtest`). The DMA
  runaway check's bound was `sizeof(sndbuffers)`, a pointer's 4 bytes, so
  every VBL restarted the DMA and mixed a buffer: 60 a second on a TT for
  49 played, a fifth of every sound cut. 2.9 -> 1.5 ms a frame.
- **Caches:** `I_Init` (supervisor) sets CACR to `$1111` on a 68030
  (TOS 3.06 boots with `$3111`): both caches and burst fills, no write
  allocate, which only matters for user mode, and the game runs in
  supervisor mode; both caches are cleared at each change and the old
  value is restored at exit. `-cacr <hex>` overrides it (see Hatari's
  burst timing below).
- **Line of sight (game logic, exact):** `P_LookForPlayers` tests the
  player's angle and distance before `P_CheckSight` (id did it after:
  either failing skips the player, and the sight check only bumps
  `validcount`). `P_DivlineSide` is inline and the sight check's BSP walk
  a loop with its own stack (TTDOOM), visiting nodes in the same order.
  Sight checks were 58% of the game logic: 9.2 -> 7.3 ms a tic (demo1).
- **Sight traversal in asm (68020+, exact):** `P_SightTrace`
  (`p_sight.c`) is `P_CrossBSPNode` and `P_CrossSubsector`'s loop as one
  routine of 282 bytes, laid out for the 256-byte instruction cache: the
  walk, the subsector's lines and the side test took turns pushing each
  other out of it as C (6 cycles an instruction). It reads compact
  records: `node_t` is 16 bytes of shorts (the WAD's whole map units;
  bounding boxes apart in `nodebbox[]`), a seg's `sightseg_t` 16 bytes
  (its linedef as a divline in shorts, laid out as a node, so one side
  routine serves both), `sightsubs[]` each subsector's records, and
  `linevalid[]` the lines' `validcount`. A line the sight line really
  crosses goes to C (`P_SightCrossedLineAt`, id's code). Sight lines
  exactly along x or y, and 68000 builds, take the C versions, which read
  the same records. Sight checks 3.9 -> 2.0 ms a tic (demo3).
- **Thinkers (exact):** `P_RunThinkers` runs `P_MobjThinkerInline`
  (`p_local.h`) for things: id's tests in id's order, with a thing that
  moves or is off the floor going to `P_MobjThinkerMoving` (id's whole
  function) and the nightmare respawn count to `P_MobjRespawnCheck`.
- **Point in subsector (68020 to 68040, exact):** `R_PointNodeWalk`
  (`r_main.c`), `R_PointInSubsector`'s walk with `R_PointOnSide` inline,
  in asm on the compact nodes: 95 -> about 40 cycles a node. A 68060
  (`fixedmul_muls64` clear) keeps the C.
  With these three the game logic is 9.2 -> 6.3 ms a tic (demo3, gcc
  4.6.4; 8.5 -> 6.0 with gcc 13 + LTO), and STDOOM's 101 -> 94 ms.
- **Wall loop in passes (68020+, same pixels):** `R_RenderSegLoop` was
  one loop of 380 bytes a textured column that reloaded a dozen globals
  each time round. Now `R_SegMarkSolid`/`R_SegMarkTwoSided` clip, mark the
  planes and note the tiers; `R_SegTierColumns` works out texture column,
  light (a table for `LIGHT_INDEX`'s multiply) and scale once for each
  column that has a tier (clipped columns skip two multiplies and the
  divide); `R_DrawSegTiers` as before. A column's scale is the seg's
  first plus the step times the columns since, in unsigned arithmetic,
  which is what id's adding came to. Walls 14.3 -> 10.0 ms a frame
  (demo3 low).
- **Span set-up (68020+, same pixels):** `R_MapStagedSpans` first brings
  the rows' cache up to date (`planerows[]`: id's four `cached` arrays as
  one 16-byte record a row), then sets the spans up from locals: nothing
  through the `ds_` globals, and `FixedMul64` (`m_fixed.h`: `FixedMul`
  without its test of `fixedmul_muls64`, made once outside). 5.7 -> 3.6
  ms.
- **Sprites as column records (68020+, same pixels):** a sprite drawn by
  `R_DrawColumn` has its posts set up as records
  (`R_SetupMaskedColumn`) and drawn by `R_DrawColumns`, as walls are. Each
  post used to go through `colfunc` and the `dc_` globals to set up and
  draw one record. Fuzz and translated sprites, and masked walls, still do.

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
- **The pair C2P trusts the dirty box:** in low detail, anything drawn into
  the 3D view at full resolution (text, patches, wipes, a new effect) must
  reach `V_MarkRect`, or its rows are converted as pairs and lose every
  other pixel. Drawers in the view must keep pixel pairs.
- **X flag loops:** between the `move.w ..,ccr` that primes a column or
  span loop and each `addx`, nothing may set X: no `add`, `sub`, `neg`,
  shifts or rotates. `and`, `or`, `move`, `tst`, `btst`, `lea`, `dbra`
  leave it.
- **Batched columns point into cached lumps:** fetching an uncached column
  can `Z_Malloc` and purge a lump an earlier record points into, so
  `R_DrawSegTiers` draws what it has set up before `R_GetColumnSlow`
  (`R_GetCachedColumn` never allocates). Checked by forcing a miss every
  7th column: identical frames.
- **gcc 4.6 for m68k:** inline asm can't take more data registers than are
  free (8 less the clobbers); fold flags into a CCR value rather than add
  an operand. A register variable (`register long x asm("d5")`) pins one.
- **`sprnames` ends in a NULL.** id's list had none, and `R_InitSpriteDefs`
  counts the names up to a NULL: it read past the array into whatever the
  linker put next. With LTO that wasn't a NULL, and TTDOOM crashed in
  `R_InitSprites` reading through `$FFFFFFFF`. A layout change can expose
  any such overrun: suspect one first when only a new layout crashes.
- **`I_Error` gives the VBL back** (`I_ShutdownSound`, which only restores
  `$70` if the sound VBL is installed and only touches the DMA register if
  DMA sound was started: `I_Error` can run before `I_InitSound`, and an ST
  has no such register). Every timedemo ends in `I_Error`, which used to
  leave the sound VBL running in the freed program: the machine hung once
  the next program loaded over it.
- **Load alignment.** GEMDOS keeps blocks only even, so a block can start
  2 bytes off a long, and then every long the 68030 reads from it takes two
  bus cycles. `I_AllocAligned` puts the zone and `screens` on 16 bytes, but
  the program itself loads wherever the free memory starts: 2 bytes off,
  demo3 low ran 4.4% slower (gcc 4.6.4) or 1.4% (LTO + PGO) under Hatari,
  and 8.1% or 2.6% on a real TT. Compare timings only at the same load
  alignment: the start-up line prints where the zone and the program are.

- **Lines' `validcount` is `linevalid[]`**, shared by the sight checks
  and `P_BlockLinesIterator`, as the field in `line_t` was: a sight check
  inside a blockmap walk (an action function run from a collision) bumps
  `validcount` under it, and id's game depends on which lines each then
  skips. Don't give either its own array.
- **`P_SightTrace` is laid out for the cache:** 282 bytes where the cache
  holds 256, so its first and last 26 bytes (to a line) share lines.
  First comes what runs least (the second crossing test, the on-the-line
  return, the general side test), last the pop loop. Moving a block
  changes which code evicts which: the link checks only the size (288,
  `CACHE_CHECK` in the Makefile), so profile after any change there
  (`i-miss%`, 3% as it is).
- **Game logic changes are checked by `fuzz.sh`, renderer changes by
  `rcheck.sh`** (below), not by the demos' 600 tics alone.

### Testing in Hatari

TOS images live in `tmp/tos/` (gitignored; `tos306uk.img` for TT; use EmuTOS
512k for Falcon). Run headless with `SDL_VIDEODRIVER=dummy`, pass
`--addr24 off` (after `--machine`) whenever TT-RAM is enabled and
`--alert-level fatal`, so no dialogs pop up. `--tos-res ttlow` boots a TT in
TT Low for testing `STDOOM.TOS`'s 8-plane path. Over the command FIFO,
`hatari-event keypress 1` is the "1" key: give single-digit scancodes in hex
(`0x01` is Esc). Frame checksums differ by a few sprite-edge pixels between
any two builds (layout-dependent reads), so compare renderer changes within
one binary, not across builds. The same goes for C2P changes: convert each
frame with both versions in one binary (onto a copy of the page) and
compare. Run at most 6 Hatari instances at a time, with
`--confirm-quit false`.

**Benchmarks:** `tools/bench` (work folder `tmp/bench`). `mkbuild.sh LABEL
[SRC | @REF] [stdoom]` builds a patched copy of the working tree (or a
commit: `@HEAD` is the baseline without your changes); `run.sh LABEL MODE
[prof]` runs a 600-tic timedemo headless and prints realtics, ms a frame
(N x 1000 / 35 / 600) and the BENCH STATE game-state hash. MODE is `low`
or `high` (size 10), optionally with `nodraw` (game logic a tic), `nosfx`
or `nosound`; `DEMO=demo2`, `MACHINE=falcon` (EmuTOS 512k in `$ETOS512`,
VGA) or `ste` (STDOOM), `EXTRAARGS` (`-benchshot N`, `-mixtest`,
`-c2ptest`, `-cacr X`, `-benchzone KB`, `-noblit`: no C2P or page flip,
so a run without it less one with it is the C2P) and `TAG` (to tell
parallel runs apart). The patch
(`benchpatch.py`) reads arguments from `ARGS.TXT` (Hatari can't pass them
to an autostarted program), ends demos at tic 600 and adds the dumps and
self-tests. The TT is the target: TOS 3.06, `--machine tt --mmu off
--monitor vga --memsize 2 --ttram 4 --addr24 off --cpu-exact on
--data-cache on`. Emulated time ignores host load, so `--fast-forward on`
and up to six parallel runs are fine.

- **Profiles:** `run.sh LABEL MODE prof` breaks on the first demo tic to
  start Hatari's profiler and on `I_Error` (the result) to save it and
  quit; `profagg.py <run>/log tmp/bench/nm_LABEL.txt 40` lists functions
  by cycles, with instruction-cache misses and data-cache hits. (Profile
  through the FIFO alone and the host's reaction time cuts off the first
  few hundred tics.)
- **Renderer checks:** `-benchshot N` writes `screens[0]` (`SN.RAW`) and
  the converted page (`PN.RAW`); `shots.py WAD runA runB out` compares two
  builds' frames (PNG and a difference image), `pagecheck.py` decodes the
  pages against the frames (C2P).
- **Game logic:** demos 1-3, low or `nodraw`, must print the same BENCH
  STATE hash as the baseline (demo1 `81c05110`, demo2 `a689a893`, demo3
  `c3d20ea1` with DOOM1.WAD 1.9). `-benchtics N` ends a demo at tic N
  instead of 600.
- **`fuzz.sh LABEL [BASE]`** is the wider check: `-benchfuzz SEED` plays
  generated ticcmds (a private generator: move, turn, fire, use, change
  weapon; god mode, the shareware weapons) one tic a loop, and prints a
  hash of the game state in depth every 35 tics. The script runs 3000
  tics on each of E1M1-E1M9, plain, `-respawn` and `-fast`, and compares
  the 27 streams with BASE's (`mkbuild.sh base @HEAD`). gcc 4.6.4 and gcc
  13 + LTO give the same streams; STDOOM (`MACHINE=ste`, a subset of
  maps) its own baseline's.
- **`rcheck.sh LABEL`** checks a renderer change pixel for pixel. While
  the change is made its old code stays in the source under `R_CHECK`,
  chosen by `r_oldpath`; `RCHECK=1 mkbuild.sh LABEL` defines it, and
  `-rcheck` draws every 3D view both ways and compares `screens[0]`
  (`RCHECK frames N, M differ`). The script runs demos 1-3 in low and
  high detail at sizes 10, 9 and 5 and the fuzz player on nine maps: 27
  runs, about 24,000 frames. Delete the old code once it passes. (A
  deliberate one-step lighting error showed in 107 frames of 120.)
- **Real play** (`run.sh LABEL lowplay`): `-playdemo` at a real TT's pace
  (`-benchpace 190`: the tic clock 1.9 times as fast, as Hatari's TT is)
  gives frames a second and the game's speed. `TryRunTics` runs at most 5
  tics a frame (`NetUpdate` builds no further ahead: `BACKUPTICS/2-1`), so
  below 7 fps Doom slows down rather than dropping frames, and a frame is
  R + 5T, R the timedemo frame less a tic (T, from `nodraw`).
- **`runkit.sh NAME`** runs `BENCH.TXT` lines (stdin) through `BENCH.TOS`
  under Hatari: builds at the same load alignment, `+2`, `-cacr 0111`.
- **Real hardware:** `tools/bench/ttkit.sh` builds a kit in `tmp/ttkit`:
  benchmark builds (600 tics, `BENCH STATE`) with gcc 4.6.4, gcc 13 + LTO
  and + PGO as `GCC4.TTP`, `LTO.TTP` and `PGO.TTP`, the WAD,
  `LOW.CFG`/`HIGH.CFG` (size 10, for `-config`) and `BENCH.TOS`
  (`ttbench.c`), which runs `BENCH.TXT`'s timedemos unattended (6 runs,
  about 12 minutes on a TT) and writes `RESULTS.TXT`. Each run's output
  goes to `RUNn.LOG`: `BENCH.TOS` passes `-benchlog RUNn.LOG` and the
  benchmark build sends its stdout and stderr there itself, and its
  `I_Error` doesn't wait for a key. Redirecting the handles in `BENCH.TOS`
  before `Pexec` (`Fforce`) works under Hatari, whose drive emulation
  handles it, but not on Neil's TT (TOS 3.06, the kit on an ACSI2STM
  drive, whose GemDrive mode also hooks the GEMDOS calls itself): the
  program wrote to the screen and read the keyboard, and the logs stayed
  empty. Each program loads with its
  text on 16 bytes (`+N` in `BENCH.TXT` for N past): a pad block in front
  of the largest free block, corrected after a trial load (`Pexec` mode
  3), since TOS puts the copy of the environment there first. `BENCH.TOS`
  loads and allocates in ST-RAM (`ttkit.sh` sets its program flags), so
  the zone is what it would be from the desktop: in TT-RAM it took 350K
  there, and on Neil's TT the zone came out at 1.83 MB. The benchmark
  builds print `BENCH READS`, the lumps read while the demo was timed
  (Doom doesn't precache a level for a demo, so a timedemo reads each
  texture, flat and sprite as it comes into view, and again once the zone
  has purged it): demo3, 600 tics, read 50 lumps (303K) with a 2.5 MB
  zone and 296 (979K) with 1.83 MB (`-benchzone KB` caps the zone). That
  costs nothing under Hatari's drive emulation and real time on a TT.
  `ttkit.sh` runs the finished kit once under Hatari (the TT setup above)
  and notes each run's realtics there in `BENCH.TXT`, which `RESULTS.TXT`
  gives the ratio to.

### Where the time goes (Hatari, size 10, 600-tic timedemo)

TT is the target (2 MB + 4 MB TT-RAM), demo1, ms a frame:

|                                      | TT low       | TT high       | Falcon VGA low | Falcon VGA high |
| ------------------------------------ | ------------ | ------------- | -------------- | --------------- |
| Frame                                | 111 -> 86    | 159 -> 127    | 259 -> 195     | 369 -> 284      |
| C2P                                  | 27 -> 22     | 27 -> 27      |                |                 |
| Column drawing (walls, sprites, sky) | 24 -> 19     | 45 -> 35      |                |                 |
| Seg loop, texture fetch              | 10 -> 8      | 18 -> 15      |                |                 |
| Span drawing                         | 21 -> 15     | 36 -> 28      |                |                 |
| Span set-up                          | 6 -> 7       | 7 -> 7        |                |                 |
| Fixed-point helpers (calls)          | 6 -> 0       | 7 -> 0        |                |                 |
| Sound mixing                         | 3.2 -> 1.6   | 3.4 -> 1.9    |                |                 |
| Line of sight                        | 5.4 -> 3.4   | 5.4 -> 3.4    |                |                 |
| Everything else                      | 9 -> 9       | 9 -> 10       |                |                 |

Before: `00774e6`. After: the optimisations above, 2026-11-09. Demos 2 and
3: TT low 118 -> 92 and 130 -> 101, high 168 -> 135 and 186 -> 150. Game
logic a tic (`nodraw`): 9.2 -> 7.3, 8.8 -> 7.9, 10.9 -> 9.2. (The
real-play figures once given here took Hatari's times for a TT's and
every due tic as run: see "On a real TT" and "Real play".)

**2026-11-12, game logic and the renderer's set-up** (the sight
traversal, thinkers, point walk, wall loop, span set-up and sprite
records above), demo3, before -> after:

|                                  | gcc 4.6.4       | gcc 13 + LTO   |
| -------------------------------- | --------------- | -------------- |
| Game logic a tic                 | 9.1 -> 6.3 ms   | 8.5 -> 6.0     |
| Timedemo frame, low              | 100.8 -> 90.8   | 96.3 -> 89.7   |
| Timedemo frame, high             | 149.8 -> 133.1  | 141.8 -> 133.0 |
| Real play at a TT's pace, low    | 3.73 -> 4.44 fps | 3.92 -> 4.52  |
| Real play at a TT's pace, high   | 2.76 -> 3.24    | 2.92 -> 3.28   |
| Game speed in that play, low     | 54% -> 63%      | 56% -> 65%     |
| Game speed in that play, high    | 40% -> 47%      | 42% -> 47%     |

Demo hashes and all 27 fuzz streams unchanged; `rcheck.sh` no pixel in
24,291 frames; `-mixtest` and `-c2ptest` pass. The gcc 13 gain holds
loaded 2 bytes off (96.1 -> 90.3) and with `-cacr 0111` (87.0 -> 80.6).
Falcon VGA low (demo1): 194.9 -> 169.7 ms. STDOOM: game logic 101 -> 94
ms a tic, a frame 877 -> 871. In low detail the frame is now C2P 22 ms, span
drawing 12, column drawing 14, walls 10, span set-up and planes 6.6,
sprites 4.5, game logic 6.3.

- **The drawers are now memory-bound** under Hatari: low-detail spans hit
  the data cache 58% of the time (the 512-byte word colormaps can't fit
  the 256-byte cache), high-detail 68%, columns 84%. Fewer instructions
  bought less than their count suggests.
- **Hatari's TT memory timing is not the TT's.** It treats TT ST-RAM as a
  16-bit bus shared with the video; a real TT gives the CPU a 32-bit slot
  every 500 ns (about 8 MB/s), so a 320x480 page (153,600 bytes) takes at
  least 19 ms of stores there. It charges 2 clocks for each of a burst
  fill's three extra longs, where a 68030 can take 1: with data bursts off
  (`-cacr 0111`) TTDOOM runs 9-10% faster under Hatari and no faster on a
  real TT (below). Compare builds in Hatari, but take absolute times,
  anything bound by ST-RAM and gains of a few percent to a real TT: LTO's
  2.3% didn't show there.
- **Write allocate** off (`-cacr 1111` against TOS's `3111`): 0.5% under
  Hatari.
- **-O2 times the same as -O3; -Os is 7% slower.**

### gcc 13: LTO and PGO (TTDOOM)

- **Toolchain.** `make ttdoom TOOLCHAIN=mintelf` builds with gcc 13.3
  (`m68k-atari-mintelf`, in atarist-toolkit-docker from its `mintelf`
  branch: until that image is published, build it and pass
  `STCMD_IMAGE_TAG`). Each variant has its own `build/` and `dist/` folder
  (`mintelf`, `mintelf-lto`, `mintelf-lto-pgo`); the default stays gcc
  4.6.4, byte for byte. It links fdlibm (`-lm`: the vendored `src/deps`
  libm.a are a.out), and ELF C symbols have no leading underscore, so asm
  names them through `__USER_LABEL_PREFIX__` (`C2P8B_SYM`). It defines
  `_GNU_SOURCE`: MiNTLib's headers hide `usleep`, `strcasecmp` and
  `strupr` under `-std=c99`, and gcc 14 on rejects implicit declarations.
- **LTO=1** inlines across files. `atari_c2p.c` is compiled without it:
  an LTO object holds its top-level asm only as intermediate code, where
  `CACHE_CHECK` can't measure the loops (`p_sight.c` likewise, for
  `P_SightTrace`).
- **PGO:** `tools/bench/pgo.sh` builds with `PGO=gen`, runs the training
  timedemos (demo1 and demo2, low and high; demo3 stays unseen), merges
  the profiles and rebuilds with `PGO=use`: `pgo.sh LABEL` for a benchmark
  build (demos end at tic 600), `pgo.sh --dist` for this tree as it is,
  the demos played in full, into `dist/mintelf-lto-pgo/TTDOOM.TOS`. The
  profiling build runs the same code as the release: the game source has
  two hooks (`src/pgo.h`), null unless `src/pgo.c`, which only `PGO=gen`
  links, sets them. `main` takes its arguments from `ARGS.TXT` (Hatari
  can't pass them to an autostarted program), and `G_CheckDemoStatus`
  writes the profile when a timedemo ends. How the profile gets out of
  TOS: gcc's exit-time writer wants one long-named file per object (TOS
  has 8.3 names), and `I_Error` waits for a key before exiting, so the
  objects register their counters in a `gcov_info` section
  (`-fprofile-info-section`) and `pgo.c` writes them as one stream,
  `PGO.DAT`.
  `gcov-tool merge-stream` turns the streams into `.gcda` files beside the
  objects, and `gcdasum.py` writes in the whole-program summary the stream
  leaves out (with `runs=0` gcc ignores a profile, and with LTO crashes).
  No value profiling (its counters are thread-local, which a TOS program
  can't link), and `-fprofile-correction` (the sound VBL's counters run on
  while the profile is written).
- **Results** (Hatari, ms a frame; demo hashes unchanged, `-mixtest` and
  `-c2ptest` pass):

  |                    | gcc 4.6.4 | gcc 13 | + LTO | + LTO + PGO |
  | ------------------ | --------- | ------ | ----- | ----------- |
  | TT low, demo1      | 86.6      | 85.3   | 83.7  | 81.8        |
  | TT low, demo2      | 92.3      | 91.3   | 88.5  | 86.6        |
  | TT low, demo3      | 101.8     | 100.5  | 96.4  | 94.4        |
  | TT high, demo1     | 127.7     | 126.0  | 123.4 | 120.9       |
  | Falcon VGA low     | 194.7     | 196.5  | 193.4 | 184.9       |
  | Game logic a tic   | 7.3       | 6.7    | 6.9   | 6.4         |

  LTO + PGO: 5-7% a timedemo frame (demo3, which PGO didn't train on,
  gains the most), about 9% in real play (TT low ~106 -> ~97 ms). Only the
  C side gains: the drawers, C2P and mixer are asm.

  The `dist` builds over whole demos (TT, size 10; `pgo.sh --dist` trained
  on demos 1 and 2), gcc 4.6.4 -> + LTO -> + LTO + PGO: demo1 low 86.9 ->
  85.2 -> 82.9 ms, high 131.9 -> 128.6 -> 124.5; demo3 low 87.7 -> 85.9
  -> 83.5. Every demo plays to its last tic.

### On a real TT (2026-11-12)

Neil's TT030 (TOS 3.06, 2 MB + 4 MB TT-RAM, VGA, the kit on an ACSI2STM
drive in GemDrive mode), `ttkit.sh`'s benchmark builds: demo3, 600 tics,
size 10, the program on 16 bytes unless `+2`. Every run gave demo3's hash,
`c3d20ea1`, and identical runs differed by at most 0.08%.

The second round, BENCH.TOS in ST-RAM. The TT's own drivers and
accessories hold 515K of TT-RAM, so the zone was 2.15 MB, against
Hatari's full 2.5 MB:

|                                 | TT, ms | Hatari | TT / Hatari |
| ------------------------------- | ------ | ------ | ----------- |
| gcc 4.6.4, low                  | 183.7  | 97.2   | 1.89        |
| gcc 13 + LTO, low               | 184.1  | 95.5   | 1.93        |
| + PGO, low                      | 175.8  | 93.0   | 1.89        |
| + PGO, high                     | 253.6  | 138.6  | 1.83        |
| + PGO, low, 1.83 MB zone        | 182.1  | 93.0   | 1.96        |
| + PGO, game logic a tic         | 14.8   | 7.3    | 2.03        |

The third round, the C2P (a run less its `-noblit` run) and demo1:

|                                | TT, ms | Hatari | TT / Hatari |
| ------------------------------ | ------ | ------ | ----------- |
| demo3 low: frame               | 176.9  | 93.2   | 1.90        |
| demo3 low: C2P                 | 33.5   | 21.2   | 1.58        |
| demo3 high: frame              | 254.7  | 138.9  | 1.83        |
| demo3 high: C2P                | 37.7   | 25.7   | 1.47        |
| demo1 low: frame               | 147.8  | 80.9   | 1.83        |
| demo1: game logic a tic        | 12.0   | 5.9    | 2.04        |

The first round, BENCH.TOS in TT-RAM, so the zone was 1.83 MB:

|                          | TT, ms | Hatari | TT / Hatari |
| ------------------------ | ------ | ------ | ----------- |
| gcc 4.6.4, low           | 191.6  | 97.5   | 1.97        |
| gcc 13 + LTO, low        | 191.4  | 95.2   | 2.01        |
| + PGO, low               | 182.1  | 93.0   | 1.96        |
| + PGO, high              | 259.8  | 138.5  | 1.88        |
| + PGO, low, `+2`         | 186.9  | 93.9   | 1.99        |
| gcc 4.6.4, low, `+2`     | 207.0  | 100.7  | 2.06        |
| + PGO, low, `-cacr 0111` | 182.2  | 84.5   | 2.16        |

- **A TT takes about twice Hatari's time:** 1.9 for a frame and 2.0 for
  the game logic. Another project measured 2.2 for compiled code and 1.5
  for a loop that stays in the instruction cache. High detail's extra
  time, mostly the drawers' inner loops, is 1.7 times Hatari's.
- **Real play on a TT is in slow motion at size 10.** `TryRunTics` runs
  at most 5 tics a frame (see "Real play"), so with those builds a frame
  was R + 5T: 157 + 74 = 231 ms in low detail (demo3), 4.3 fps with the
  game at 62% of its speed, and 309 ms, 3.2 fps and 46%, in high detail.
  Drawing is 54% of that frame, the game logic 32% and the C2P 14%.
  (Hatari at a TT's pace, `lowplay`, agrees: 3.7 fps and 54% for the gcc
  4.6.4 build.)
- **The C2P is a fifth of a timedemo frame on a TT:** 33.5 ms in low
  detail (19%), 37.7 ms in high (15%). It takes 1.5-1.6 times Hatari's
  time, as a loop in the instruction cache does, against 1.9-2.0 for the
  rest of the frame: not bound by ST-RAM's stores (a page is at least 19
  ms of them).
- **PGO is what pays on a TT:** 4-5% over gcc 4.6.4. LTO alone is no
  faster there (0.2% slower in the second round, though 2.3% faster under
  Hatari), so the release (gcc 13 + LTO, no PGO) runs at gcc 4.6.4's speed.
- **Lump reads:** demo3 read 126 lumps (439K) with the 2.15 MB zone and
  296 (979K) with 1.83 MB. The extra 170 took 3.6% of the run, about 22 ms
  a lump from ACSI2STM in GemDrive mode. Hatari's full zone reads 50
  lumps, and its reads take no emulated time. In real play the level is
  precached, but a zone that can't hold it re-reads lumps mid-level.
- **Load alignment costs 2.5 times what Hatari shows**: 2.6% (PGO) and
  8.1% (gcc 4.6.4) against 1.0% and 3.3%.
- **Data bursts:** `-cacr 0111` changes nothing on a TT, where Hatari runs
  9% faster. The default stays `1111`.

### Possible follow-ups (TTDOOM)

- **Time on a real Falcon.** A TT is timed above. A real Falcon refills its
  instruction cache from ST-RAM over a 16-bit bus, so the loops' cache fit
  may count for more there.
- **The rest of the 2026-11-12 plan** (exact, to be checked the same
  way): masked walls as column records (flushing before an uncached
  column, as `R_DrawSegTiers` does); `R_SortVisSprites` as ADoom's
  insertion sort; `R_MakeSpans` with trip counts worked out up front
  (ADoom); `R_PointToAngle` as octant selection plus one `SlopeDiv`; the
  wall tiers' column fetch (`R_RenderSegLoop`, 4.5 ms, a table chain a
  tier); `R_MapStagedSpans` in asm; span stores four pixels to a
  `move.l` and columns unrolled x4 (ADoom's `R_DrawSpan_040` and
  `R_DrawColumn_040`: packing by `move.w`/`move.b`/`swap` leaves X
  alone, and ending the loop on an address compare frees its counter).
- **Game logic, what's left:** the thinker loop (1.2 ms a tic: it reads
  six cache lines a thing; `mobj_t` reordered would read three, but
  savegames store the struct raw, so they would have to be written
  through the old layout), then collision (`P_CheckPosition` and the
  blockmap iterators, 1 ms in 100 bytes here and there). Skipping the two
  multiplies of a sight side test when the signs decide it was worked
  out and left: about 1% of a tic for 40 bytes of the cached routine.
- **ADoom** (1.4, `mheyer32/ADoom`) has nothing else to take: its
  renderer asm is id's code by hand (a divide a column, no batching,
  `FixedMul` through a pointer), its game logic id's, its FPU fixed point
  for the 68060 only, and its compare-buffer C2P costs 13 ms a frame on
  a 25 MHz 68030 before it saves anything.
- **PGO for the release** (it is 4-5% on a TT, where LTO alone gained
  nothing): CI builds TTDOOM with gcc 13 + LTO, and the training needs
  Hatari, a TOS image and the WAD, which CI doesn't have (`pgo.sh --dist`
  runs locally).
- **Hot static data on 16 bytes whatever the load address.** The program
  loads where GEMDOS puts it, and 2 bytes off a long costs 2.6% on a TT.
  Find which static arrays matter (Hatari profiles of a `+2` run against
  an aligned one) and move them into an aligned allocation.
- **Spread the zoomed views' stores.** `C2P8_MERGE_FN` (sizes 3-5) still
  stores 2, 4 or 8 copies of each group in one burst. With more stores than
  merges, measure whether spreading still pays.
- **On a real TT:** the mixer's ST-RAM saving, which Hatari understates.
  (The C2P there scales like an in-cache loop, 1.5-1.6 times Hatari, so
  ST-RAM's stores don't bound it.)
- **TT line doubling in hardware (rejected).** Rewriting the TT's video
  counter from Timer B each line would display each line twice for free,
  but costs about 14,400 interrupts a second (about 7% of the CPU) to
  save the second line's stores (1.4 ms in Hatari). Only worth it if the
  C2P were bound by ST-RAM, which a real TT shows it isn't.
- **Scrambled chunky layout (rejected).** Pixels stored in the order the
  C2P's 8-bit pass produces would drop that pass (19% of a group's
  instructions) and cost columns nothing (`columnofs`), but spans,
  patches, wipes, the automap and screenshots would all need the order,
  and a span loop absorbing it doesn't fit the instruction cache.
- **The toolchain, what's left** (gcc 13, LTO and PGO: see above; PGO for
  the release has its own item): value profiling (needs thread-local
  storage in a TOS program); tuning flags for gcc 13; vasm modules for the
  drawers and C2P.
- **`-mfastcall`: measured, not adopted.** Vincent Rivière's gcc 4.6.4 and
  13.3 (the toolkit's) don't have it; Thorsten Otto's Crossmint gcc does,
  with fastcall builds of libgcc, MiNTLib and fdlibm (what upstream STDOOM
  uses). With his gcc 15.2 + LTO under Hatari, `FASTCALL=1` gained 1-1.5%
  (TT low demo1 83.9 -> 83.3 ms, demo3 97.1 -> 95.6, high 125.0 -> 123.0),
  and gcc 15 was a little slower than gcc 13, so the net gain over gcc 13
  + LTO was under 1%. Against it: his toolchain installs where Vincent's
  gcc 13 does (one or the other in the toolkit), is prebuilt for x86_64
  only, and his `m68020-60` MiNTLib runs FPU instructions in `sscanf`
  (TTDOOM's config), crashing a Falcon without an FPU. Kept, so a fastcall
  toolchain still builds: `FASTCALL=1` gives its own build folders, and the
  C2P box loops are `cdecl` under `__FASTCALL__` (their asm reads the
  stack; gcc's fastcall passes pointers in a0/a1 and integers in d0-d2).
  STDOOM would also need `sidecart_stubs.S` changed.
- **Smaller wins left:** the `divu.l` for `iscale`, now only for
  columns that draw (an approximation would be sub-texel: not adopted
  without Neil seeing frame differences); `R_StoreWallRange` (2.2 ms, 3K
  of straight-line code).
- **Falcon true colour (unexplored).** The 16-bit mode needs no C2P but
  doubles the bytes on a bus the video also reads. The cheap first test is
  to keep the 8-bit renderer and replace the Falcon C2P with an 8-to-16-bit
  table copy. If that isn't faster than the 53 ms C2P, the idea is dead. If
  it is, render straight into the page: `colormaps_w` shows the drawers
  barely change. Fuzz darkens by colour index, and palette flashes would
  need the table rebuilt. Confirm on a real Falcon, since Hatari may not
  model the video's share of the bus. The DSP is also unexplored.
- **Planar rendering (no C2P): rejected.** Walls and sprites are
  one-pixel-wide columns, so each pixel would be 8 bit read-modify-writes
  in ST-RAM, where the C2P costs a fraction of a store. It would also mean
  rewriting every drawer, fuzz, patches and wipes, to save at most the C2P.
  The TT has no chunky mode, so Falcon true colour is the only C2P-free
  route.

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
