################################################################
#
# STDOOM (Atari ST) and TTDOOM (Atari TT / Falcon)
#
#   make              dist/STDOOM.TOS and dist/TTDOOM.TOS
#   make stdoom       dist/STDOOM.TOS
#   make ttdoom       dist/TTDOOM.TOS
#   make clean        removes build/ and those programs (not the WADs)
#   make ttdoom TOOLCHAIN=mintelf
#                     dist/mintelf/TTDOOM.TOS, built with gcc 13; add LTO=1
#                     for link-time optimisation (dist/mintelf-lto)
#   tools/bench/pgo.sh --dist
#                     dist/mintelf-lto-pgo/TTDOOM.TOS: gcc 13, LTO and
#                     profile-guided optimisation (PGO=gen, then PGO=use),
#                     trained on timedemos under Hatari
#   make sidecart     the DOOM Accelerator firmware and its test apps
#                     (sidecart/Makefile), e.g. make sidecart debug
#
# Sources are in src/, objects in build/, programs in dist/ beside the WADs.
# The m68k-atari-mint toolchain comes from atarist-toolkit-docker: run on the
# host, this Makefile runs the game build in it through stcmd.
#
################################################################

# TOOLCHAIN=mintelf builds with the toolkit's gcc 13 (m68k-atari-mintelf)
# instead of gcc 4.6.4 (m68k-atari-mint), into build/mintelf and dist/mintelf.
TOOLCHAIN = mint
CC = m68k-atari-$(TOOLCHAIN)-gcc
NM = m68k-atari-$(TOOLCHAIN)-nm

.DEFAULT_GOAL := all
.PHONY: all clean stdoom ttdoom sidecart
# With sidecart, the other goals are for sidecart/Makefile (make sidecart
# debug). Only then: make applies no pattern rule to a phony goal.
ifneq ($(filter sidecart,$(MAKECMDGOALS)),)
.PHONY: $(filter-out all clean stdoom ttdoom sidecart,$(MAKECMDGOALS))
endif

# The firmware builds on the host (ARM toolchain; its ST test apps use stcmd)
sidecart:
	$(MAKE) -C sidecart $(filter-out $@,$(MAKECMDGOALS))

ifeq ($(shell command -v $(CC) 2>/dev/null),)

# Not in the toolchain container: run the game build there
export STCMD_NO_TTY = 1
all clean stdoom ttdoom:
	stcmd make $(MAKEOVERRIDES) $@

else

SRC=src
OBJDIR=build
DISTDIR=dist
# Xpad gamepads: the lib/xpad submodule, pinned to a release. Only the
# consumer half (xpad.c): xpad_provider.c is for providers.
XPAD=lib/xpad/src

FASTCALL=0

# Add -mfastcall only when FASTCALL=1, and link the matching libm.a variant so
# the calling convention agrees between Doom and libm. It needs a toolchain
# with -mfastcall and fastcall builds of libgcc and libc too, such as
# Thorsten Otto's: the toolkit's gcc 4.6.4 and 13 have neither (AGENTS.md).
ifeq ($(FASTCALL),1)
FASTCALLFLAG=-mfastcall
LIBVARIANT=.fastcall
else
FASTCALLFLAG=
LIBVARIANT=
endif

# SFP-004 FPU library lives in the src/atarist-sfp004 submodule. -I makes
# #include "atari_sfp004.h" resolve; the vpath below lets the %.o rules find
# atari_sfp004.c there (OBJS still lists a flat $(OBJDIR)/atari_sfp004.o).
CFLAGS=-O3 -Wall -DNORMALUNIX -DLINUX -D__BIG_ENDIAN__ -DUSEASM $(FASTCALLFLAG) -std=c99 -fasm -I$(SRC)/atarist-sfp004 -I$(XPAD)
# TTDOOM: TT / Falcon, 320x240 in 256 colours, code for any 68020 to 68060.
# No -mtune=68030: it turns divides by constants into 64-bit multiplies,
# which a 68060 traps, and measured no faster on a 68030. Soft-float so it
# runs on a Falcon without an FPU (Doom's renderer is fixed-point anyway).
# No frame pointer: gcc otherwise gives every function a link/unlk, the
# per-column and per-span drawers included, and a6 is one more register.
CFLAGSTT=$(CFLAGS) -m68020-60 -msoft-float -fomit-frame-pointer -DTTDOOM -DSCREENHEIGHT=240 $(OPTFLAGS)

vpath %.c $(SRC) $(SRC)/atarist-sfp004 $(XPAD)
vpath %.S $(SRC)
LDLIBS=$(SRC)/deps/68000.soft-float$(LIBVARIANT)/libm.a
LDLIBSTT=$(SRC)/deps/68020-60.soft-float$(LIBVARIANT)/libm.a

# Another toolchain: its own build and dist folders, and its own libm
# (fdlibm), since the vendored libm.a are a.out, which an ELF link can't use.
# With gcc 13, LTO=1 adds link-time optimisation, and PGO=gen and PGO=use
# build TTDOOM to record a profile and then with it (tools/bench/pgo.sh).
# Each has its own folders too, but the two PGO steps share theirs: gcc
# looks for an object's profile beside it.
ifneq ($(TOOLCHAIN),mint)
VARIANT=$(TOOLCHAIN)$(if $(filter 1,$(LTO)),-lto)$(if $(filter 1,$(FASTCALL)),-fastcall)$(if $(PGO),-pgo)
OBJDIR=build/$(VARIANT)
DISTDIR=dist/$(VARIANT)
LDLIBS=-lm
LDLIBSTT=-lm
# gcc 14 on rejects calls to undeclared functions, and with -mfastcall
# they may not pass their arguments as the library expects: MiNTLib's
# headers declare usleep, strcasecmp and strupr only with _GNU_SOURCE
# (-std=c99 alone hides them)
OPTFLAGS+=-D_GNU_SOURCE
ifeq ($(LTO),1)
OPTFLAGS+=-flto=auto
endif
# No value profiling: its counters are thread-local, which a TOS program
# can't link (gcc reaches them through a GOT). Profile correction: the
# sound VBL's counters run on while the profile is written, so their
# counts can disagree (gcc otherwise stops: "corrupted profile info").
ifeq ($(PGO),gen)
OPTFLAGS+=-fprofile-generate -fno-profile-values -fprofile-info-section=gcov_info
endif
# PROFILE=folder: with PGO=use, that folder's .gcda files are the profile
# (tools/pgo/profile, as tools/bench/pgo.sh --dist leaves it: what the
# release is built with, where there is no Hatari to record one). A
# function changed since the profile was recorded is compiled without it,
# not refused (-Wno-coverage-mismatch).
ifeq ($(PGO),use)
OPTFLAGS+=-fprofile-use -fno-profile-values -fprofile-correction -fprofile-partial-training -Wno-missing-profile -Wno-coverage-mismatch
endif
endif

EXT=.TOS
TARGET=STDOOM$(EXT)
TARGETTT=TTDOOM$(EXT)

# not too sophisticated dependency
OBJS=				\
		$(OBJDIR)/atari_c2p.o		\
		$(OBJDIR)/atari_checkcpu.o	\
		$(OBJDIR)/atari_sfp004.o	\
		$(OBJDIR)/atari_ym.o		\
		$(OBJDIR)/i_xpad.o		\
		$(OBJDIR)/xpad.o		\
		$(OBJDIR)/sidecart_c2p.o	\
		$(OBJDIR)/sidecart_md.o		\
		$(OBJDIR)/sidecart_stubs.o	\
		$(OBJDIR)/doomdef.o		\
		$(OBJDIR)/doomstat.o		\
		$(OBJDIR)/dstrings.o		\
		$(OBJDIR)/i_system.o		\
		$(OBJDIR)/i_sound.o		\
		$(OBJDIR)/i_video.o		\
		$(OBJDIR)/i_net.o			\
		$(OBJDIR)/tables.o			\
		$(OBJDIR)/f_finale.o		\
		$(OBJDIR)/f_wipe.o 		\
		$(OBJDIR)/d_main.o			\
		$(OBJDIR)/d_net.o			\
		$(OBJDIR)/d_items.o		\
		$(OBJDIR)/g_game.o			\
		$(OBJDIR)/m_menu.o			\
		$(OBJDIR)/m_misc.o			\
		$(OBJDIR)/m_argv.o  		\
		$(OBJDIR)/m_bbox.o			\
		$(OBJDIR)/m_fixed.o		\
		$(OBJDIR)/m_swap.o			\
		$(OBJDIR)/m_cheat.o		\
		$(OBJDIR)/m_random.o		\
		$(OBJDIR)/am_map.o			\
		$(OBJDIR)/p_ceilng.o		\
		$(OBJDIR)/p_doors.o		\
		$(OBJDIR)/p_enemy.o		\
		$(OBJDIR)/p_floor.o		\
		$(OBJDIR)/p_inter.o		\
		$(OBJDIR)/p_lights.o		\
		$(OBJDIR)/p_map.o			\
		$(OBJDIR)/p_maputl.o		\
		$(OBJDIR)/p_plats.o		\
		$(OBJDIR)/p_pspr.o			\
		$(OBJDIR)/p_setup.o		\
		$(OBJDIR)/p_sight.o		\
		$(OBJDIR)/p_spec.o			\
		$(OBJDIR)/p_switch.o		\
		$(OBJDIR)/p_mobj.o			\
		$(OBJDIR)/p_telept.o		\
		$(OBJDIR)/p_tick.o			\
		$(OBJDIR)/p_saveg.o		\
		$(OBJDIR)/p_user.o			\
		$(OBJDIR)/r_bsp.o			\
		$(OBJDIR)/r_data.o			\
		$(OBJDIR)/r_draw.o			\
		$(OBJDIR)/r_main.o			\
		$(OBJDIR)/r_plane.o		\
		$(OBJDIR)/r_segs.o			\
		$(OBJDIR)/r_sky.o			\
		$(OBJDIR)/r_things.o		\
		$(OBJDIR)/w_wad.o			\
		$(OBJDIR)/wi_stuff.o		\
		$(OBJDIR)/v_video.o		\
		$(OBJDIR)/st_lib.o			\
		$(OBJDIR)/st_stuff.o		\
		$(OBJDIR)/hu_stuff.o		\
		$(OBJDIR)/hu_lib.o			\
		$(OBJDIR)/s_sound.o		\
		$(OBJDIR)/z_zone.o			\
		$(OBJDIR)/info.o				\
		$(OBJDIR)/sounds.o		\
		$(OBJDIR)/i_main.o
# TTDOOM: no SidecarTridge accelerator (ST Low only), plus the TT / Falcon
# video mode code.
SIDECART_OBJS=$(OBJDIR)/sidecart_c2p.o $(OBJDIR)/sidecart_md.o $(OBJDIR)/sidecart_stubs.o
OBJSTT=$(patsubst %.o,%.tt.o,$(filter-out $(SIDECART_OBJS),$(OBJS))) $(OBJDIR)/atari_vmode.tt.o
# PGO=gen: pgo.c writes the profile when a benchmark's demo ends
ifeq ($(PGO),gen)
OBJSTT+=$(OBJDIR)/pgo.tt.o
endif
# The box C2P's loops and the sight check's traversal are top-level asm,
# which an LTO object only holds as gcc's intermediate code until the link:
# compiled without LTO, so CACHE_CHECK can measure them in the object
$(OBJDIR)/atari_c2p.tt.o $(OBJDIR)/p_sight.tt.o: OPTFLAGS:=$(filter-out -flto%,$(OPTFLAGS))

ifeq ($(PGO),use)
ifneq ($(PROFILE),)
# gcc looks for an object's profile beside it
$(OBJSTT): | pgoprofile
.PHONY: pgoprofile
pgoprofile: | $(OBJDIR)
	cp $(PROFILE)/*.gcda $(OBJDIR)/
endif
endif

all:	 $(DISTDIR)/$(TARGET) $(DISTDIR)/$(TARGETTT)

stdoom: $(DISTDIR)/$(TARGET)
ttdoom: $(DISTDIR)/$(TARGETTT)

# dist/ also holds the WADs: remove only the programs built here
clean:
	rm -rf $(OBJDIR)
	rm -f $(DISTDIR)/$(TARGET) $(DISTDIR)/$(TARGETTT)

$(DISTDIR)/$(TARGET):	$(OBJS) | $(DISTDIR)
	$(CC) $(CFLAGS) $(OBJS) -o $@ $(LDLIBS)

# The 8-plane box C2P's loops must fit the 68030's 256-byte instruction
# cache at any alignment: at most 240 bytes from NAME_cache0 to
# NAME_cache1 (atari_c2p.c). The assembler cannot check it itself.
# The sight check's traversal (p_sight.c) is several loops that take turns:
# up to 288 bytes, so only its first and last 32 bytes or so (to a cache
# line) share the cache, which is how it is laid out.
CACHE_CHECK = $(NM) -t d $(1) | awk \
	'/_cache0$$/ { a[substr($$3, 1, length($$3) - 7)] = $$1 } \
	 /_cache1$$/ { n = substr($$3, 1, length($$3) - 7); s = $$1 - a[n]; \
	   if (s > $(2)) { print "*** " n ": " s " bytes, over $(2)"; bad = 1 } } \
	 END { exit bad }'

$(DISTDIR)/$(TARGETTT):	$(OBJSTT) | $(DISTDIR)
	@$(call CACHE_CHECK,$(OBJDIR)/atari_c2p.tt.o,240)
	@$(call CACHE_CHECK,$(OBJDIR)/p_sight.tt.o,288)
	$(CC) $(CFLAGSTT) $(OBJSTT) -o $@ $(LDLIBSTT)

$(OBJDIR)/%.tt.o:	%.c | $(OBJDIR)
	$(CC) $(CPPFLAGS) $(CFLAGSTT) -c $< -o $@

$(OBJDIR)/%.o:	%.c | $(OBJDIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@

# Assembly stubs (.S, preprocessed) for the SidecarTridge protocol: STDOOM only
$(OBJDIR)/%.o:	%.S | $(OBJDIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@

# Assembly listings on demand, e.g. stcmd make build/r_draw.tt.s
$(OBJDIR)/%.tt.s: %.c | $(OBJDIR)
	$(CC) $(CPPFLAGS) $(CFLAGSTT) -c $< -S -fverbose-asm -o $@

$(OBJDIR)/%.s: %.c | $(OBJDIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -S -fverbose-asm -o $@

$(OBJDIR) $(DISTDIR):
	mkdir -p $@

endif
