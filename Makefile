################################################################
#
# STDOOM (Atari ST) and TTDOOM (Atari TT / Falcon)
#
#   make              dist/STDOOM.TOS and dist/TTDOOM.TOS
#   make stdoom       dist/STDOOM.TOS
#   make ttdoom       dist/TTDOOM.TOS
#   make clean        removes build/ and those programs (not the WADs)
#   make sidecart     the DOOM Accelerator firmware and its test apps
#                     (sidecart/Makefile), e.g. make sidecart debug
#
# Sources are in src/, objects in build/, programs in dist/ beside the WADs.
# The m68k-atari-mint toolchain comes from atarist-toolkit-docker: run on the
# host, this Makefile runs the game build in it through stcmd.
#
################################################################

CC = m68k-atari-mint-gcc

.DEFAULT_GOAL := all
.PHONY: all clean stdoom ttdoom sidecart
# Extra goals are for sidecart/Makefile (make sidecart debug)
.PHONY: $(filter-out all clean stdoom ttdoom sidecart,$(MAKECMDGOALS))

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
# the calling convention agrees between Doom and libm.
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
CFLAGS20=$(CFLAGS) -m68020-60 -mtune=68020 -msoft-float
CFLAGS2F=$(CFLAGS) -m68020-60 -mtune=68020 -m68881
# TTDOOM: TT / Falcon, 320x240 in 256 colours, 68030 code. Soft-float so it
# runs on a Falcon without an FPU (Doom's renderer is fixed-point anyway).
CFLAGSTT=$(CFLAGS) -m68020-60 -mtune=68030 -msoft-float -DTTDOOM -DSCREENHEIGHT=240

vpath %.c $(SRC) $(SRC)/atarist-sfp004 $(XPAD)
vpath %.S $(SRC)
LDLIBS=$(SRC)/deps/68000.soft-float$(LIBVARIANT)/libm.a
LDLIBS20=$(SRC)/deps/68020-60.soft-float$(LIBVARIANT)/libm.a
LDLIBS2F=$(SRC)/deps/68020-60.68881$(LIBVARIANT)/libm.a
LDLIBSTT=$(LDLIBS20)

EXT=.TOS
TARGET=STDOOM$(EXT)
TARGET20=STDOOM20$(EXT)
TARGET2F=STDOOM2F$(EXT)
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
OBJS20=$(OBJS:%.o=%.20.o)
OBJS2F=$(OBJS:%.o=%.2f.o)
# TTDOOM: no SidecarTridge accelerator (ST Low only), plus the TT / Falcon
# video mode code.
SIDECART_OBJS=$(OBJDIR)/sidecart_c2p.o $(OBJDIR)/sidecart_md.o $(OBJDIR)/sidecart_stubs.o
OBJSTT=$(patsubst %.o,%.tt.o,$(filter-out $(SIDECART_OBJS),$(OBJS))) $(OBJDIR)/atari_vmode.tt.o

all:	 $(DISTDIR)/$(TARGET) $(DISTDIR)/$(TARGETTT) # $(DISTDIR)/$(TARGET20) $(DISTDIR)/$(TARGET2F)

stdoom: $(DISTDIR)/$(TARGET)
ttdoom: $(DISTDIR)/$(TARGETTT)

# dist/ also holds the WADs: remove only the programs built here
clean:
	rm -rf $(OBJDIR)
	rm -f $(DISTDIR)/$(TARGET) $(DISTDIR)/$(TARGET20) $(DISTDIR)/$(TARGET2F) $(DISTDIR)/$(TARGETTT)

$(DISTDIR)/$(TARGET):	$(OBJS) | $(DISTDIR)
	$(CC) $(CFLAGS) $(OBJS) -o $@ $(LDLIBS)

$(DISTDIR)/$(TARGET20):	$(OBJS20) | $(DISTDIR)
	$(CC) $(CFLAGS20) $(OBJS20) -o $@ $(LDLIBS20)

$(DISTDIR)/$(TARGET2F):	$(OBJS2F) | $(DISTDIR)
	$(CC) $(CFLAGS2F) $(OBJS2F) -o $@ $(LDLIBS2F)

$(DISTDIR)/$(TARGETTT):	$(OBJSTT) | $(DISTDIR)
	$(CC) $(CFLAGSTT) $(OBJSTT) -o $@ $(LDLIBSTT)

$(OBJDIR)/%.tt.o:	%.c | $(OBJDIR)
	$(CC) $(CPPFLAGS) $(CFLAGSTT) -c $< -o $@

$(OBJDIR)/%.2f.o:	%.c | $(OBJDIR)
	$(CC) $(CPPFLAGS) $(CFLAGS2F) -c $< -o $@

$(OBJDIR)/%.20.o:	%.c | $(OBJDIR)
	$(CC) $(CPPFLAGS) $(CFLAGS20) -c $< -o $@

$(OBJDIR)/%.o:	%.c | $(OBJDIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@

# Assembly stubs (.S, preprocessed) for the SidecarTridge protocol. Pure 68000,
# CPU-agnostic, but per-variant rules keep the OBJS20/OBJS2F derivation working.
$(OBJDIR)/%.2f.o:	%.S | $(OBJDIR)
	$(CC) $(CPPFLAGS) $(CFLAGS2F) -c $< -o $@

$(OBJDIR)/%.20.o:	%.S | $(OBJDIR)
	$(CC) $(CPPFLAGS) $(CFLAGS20) -c $< -o $@

$(OBJDIR)/%.o:	%.S | $(OBJDIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@

# Rule for compiling to assembly on demand
$(OBJDIR)/%.2f.s: %.c | $(OBJDIR)
	$(CC) $(CPPFLAGS) $(CFLAGS2F) -c $< -S -fverbose-asm -o $@

$(OBJDIR)/%.20.s: %.c | $(OBJDIR)
	$(CC) $(CPPFLAGS) $(CFLAGS20) -c $< -S -fverbose-asm -o $@
	
$(OBJDIR)/%.s: %.c | $(OBJDIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -S -fverbose-asm -o $@

$(OBJDIR) $(DISTDIR):
	mkdir -p $@

endif
