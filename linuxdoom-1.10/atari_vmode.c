/*
 * TTDOOM display mode: 320x240 in 256 colours on a TT or a Falcon.
 * See atari_vmode.h for the layouts.
 */

#include <string.h>
#include <mint/osbind.h>
#include <mint/falcon.h>
#include <mint/cookie.h>

#include "i_system.h"
#include "i_sound.h"
#include "atari_vmode.h"

#ifndef C__MCH
#define C__MCH 0x5f4d4348L /* '_MCH' */
#endif

/* _MCH upper word */
#define MACHINE_TT      2
#define MACHINE_FALCON  3

#define TT_PALETTE      ((volatile unsigned short *)0xFFFF8400L)
#define FALCON_PALETTE  ((volatile unsigned long *)0xFFFF9800L)
#define ST_PALETTE      ((volatile unsigned short *)0xFFFF8240L)
/* MFP GPIP bit 7 on a TT: monitor detect (clear for monochrome) XOR the DMA
 * sound "playing" line, so it reads inverted while DMA sound plays. */
#define MFP_GPIP        (*(volatile unsigned char *)0xFFFFFA01L)
#define DMASND_CTRL     (*(volatile unsigned char *)0xFFFF8901L)
#define FRCLOCK         (*(volatile long *)0x466L)

#define TT_LOW_REZ      7
#define TT_LOW_BYTES    (320L * 480)
/* Falcon RGB / TV overscan: 384 pixels a line, picture 32 pixels in */
#define FALCON_OVERSCAN_STRIDE  384
#define FALCON_OVERSCAN_LEFT    32

static int vmode_machine;           /* 0 = not set, else MACHINE_TT/FALCON */
static void *old_logbase;
static void *old_physbase;
static short old_rez;               /* TT */
static short old_vmode;             /* Falcon VsetMode word */
static unsigned short old_tt_palette[256];
static unsigned long old_falcon_palette[256];
static unsigned short old_st_palette[16];
static void *screen_alloc;
static int tt_colour_monitor = 1;   /* read at startup, before sound plays */

static int machine_type(void)
{
    long mch = 0;

    if (Getcookie(C__MCH, &mch) != C_FOUND)
        return 0;
    return (int)(mch >> 16);
}

/* Supexec: 1 on a colour monitor. Read before STDOOM starts DMA sound, and
 * corrected for the play line in case something else is playing. */
static long read_tt_colour_monitor(void)
{
    int bit7 = (MFP_GPIP & 0x80) != 0;
    int playing = (DMASND_CTRL & 1) != 0;

    return bit7 ^ playing;
}

static int refuse(const char *why)
{
    (void)Cconws(why);
    (void)Cconws("Press any key.\r\n");
    (void)Cnecin();
    return 0;
}

int atari_vmode_supported(void)
{
    int machine = machine_type();

    if (machine == MACHINE_TT)
    {
        tt_colour_monitor = (int)Supexec(read_tt_colour_monitor);
        if (!tt_colour_monitor)
            return refuse("TTDOOM.TOS needs a colour monitor (TT Low,\r\n"
                          "256 colours).\r\n");
        return 1;
    }
    if (machine == MACHINE_FALCON)
    {
        if (VgetMonitor() == MON_MONO)
            return refuse("TTDOOM.TOS needs a colour monitor or TV\r\n"
                          "(256 colours).\r\n");
        return 1;
    }
    return refuse("TTDOOM.TOS needs an Atari TT or Falcon.\r\n"
                  "On an ST or STE, run STDOOM.TOS instead.\r\n");
}

void atari_vmode_set(atari_vmode_t *m)
{
    int machine = machine_type();
    unsigned char *page;
    long bytes;
    short mode = 0;
    short monitor;
    int i;

    memset(m, 0, sizeof(*m));

    if (machine == MACHINE_TT)
    {
        /* Checked at startup (atari_vmode_supported): DMA sound is
         * playing now, which inverts the monitor-detect bit. */
        if (!tt_colour_monitor)
            I_Error("TTDOOM needs a colour monitor (TT Low, 256 colours)");
        bytes = TT_LOW_BYTES;
        m->stride = 640;    /* each picture line on two 320-byte scanlines */
        m->dbl = 1;
    }
    else if (machine == MACHINE_FALCON)
    {
        monitor = VgetMonitor();
        if (monitor == MON_MONO)
            I_Error("TTDOOM needs a colour monitor or TV (256 colours)");
        old_vmode = VsetMode(-1);
        if (monitor == MON_VGA)
        {
            /* 320x480 with the VIDEL doubling lines: 320x240 */
            mode = BPS8 | COL40 | VGA | VERTFLAG;
            m->stride = 320;
        }
        else
        {
            /* 240 lines on RGB / TV take overscan. Keep the desktop's PAL
             * or NTSC: a set or monitor made for one may not sync to the
             * other. */
            mode = BPS8 | COL40 | (old_vmode & PAL) | OVERSCAN;
            m->stride = FALCON_OVERSCAN_STRIDE;
        }
        bytes = VgetSize(mode);
        m->falcon = 1;
    }
    else
    {
        I_Error("TTDOOM needs an Atari TT or Falcon");
        return;
    }

    /* Video memory must be ST-RAM; align to 256 bytes as the video base
     * registers expect. */
    screen_alloc = (void *)Mxalloc(bytes + 256, 0);
    if ((long)screen_alloc <= 0)
    {
        screen_alloc = NULL;
        I_Error("TTDOOM: not enough ST-RAM for a %ld byte screen", bytes);
    }
    page = (unsigned char *)(((unsigned long)screen_alloc + 255) & ~255UL);
    memset(page, 0, bytes);

    /* The desktop's mode and colours, before the mode change resets them */
    old_logbase = Logbase();
    old_physbase = Physbase();
    old_rez = Getrez();
    for (i = 0; i < 16; i++)
        old_st_palette[i] = ST_PALETTE[i];
    /* The mode change waits for a VBL, which STDOOM's sound handler (not
     * chained to the system's) would never let TOS see. */
    I_SoundSuspendVbl();
    if (m->falcon)
    {
        for (i = 0; i < 256; i++)
            old_falcon_palette[i] = FALCON_PALETTE[i];
        (void)VsetScreen(page, page, 3, mode);
        m->screen = page + ((mode & OVERSCAN) ? FALCON_OVERSCAN_LEFT : 0);
    }
    else
    {
        for (i = 0; i < 256; i++)
            old_tt_palette[i] = TT_PALETTE[i];
        (void)Setscreen(page, page, TT_LOW_REZ);
        m->screen = page;
    }
    I_SoundResumeVbl();
    vmode_machine = machine;
}

void atari_vmode_restore(void)
{
    long frame, n;
    int i;

    if (!vmode_machine)
        return;

    /* As in atari_vmode_set: TOS's VBL must run for the mode change, and
     * for the wait below (I_Error gets here with the sound VBL in place). */
    I_SoundSuspendVbl();
    if (vmode_machine == MACHINE_FALCON)
        (void)VsetScreen(old_logbase, old_physbase, 3, old_vmode);
    else
        (void)Setscreen(old_logbase, old_physbase, old_rez);

    /* The colours after the next VBL, which lands any the XBIOS still holds
     * for the mode change. Bounded: the VBL may be masked on an error path. */
    frame = FRCLOCK;
    for (n = 0; n < 400000L && FRCLOCK == frame; n++)
        ;

    if (vmode_machine == MACHINE_FALCON)
    {
        for (i = 0; i < 256; i++)
            FALCON_PALETTE[i] = old_falcon_palette[i];
        for (i = 0; i < 16; i++)
            ST_PALETTE[i] = old_st_palette[i];
    }
    else
    {
        /* The 16 ST registers are a bank of these 256 on a TT */
        for (i = 0; i < 256; i++)
            TT_PALETTE[i] = old_tt_palette[i];
    }
    I_SoundResumeVbl();

    vmode_machine = 0;
    if (screen_alloc)
    {
        (void)Mfree(screen_alloc);
        screen_alloc = NULL;
    }
}
