// Emacs style mode select   -*- C++ -*- 
//-----------------------------------------------------------------------------
//
// $Id:$
//
// Copyright (C) 1993-1996 by id Software, Inc.
//
// This source is available for distribution and/or modification
// only under the terms of the DOOM Source Code License as
// published by id Software. All rights reserved.
//
// The source is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// FITNESS FOR A PARTICULAR PURPOSE. See the DOOM Source Code License
// for more details.
//
// $Log:$
//
// DESCRIPTION:
//
//-----------------------------------------------------------------------------

static const char
rcsid[] = "$Id: m_bbox.c,v 1.1 1997/02/03 22:45:10 b1 Exp $";

#include <mint/osbind.h>
#include <mint/sysvars.h>
#include <mint/basepage.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include <stdarg.h>
#include <sys/time.h>
#include <unistd.h>

#include "doomdef.h"
#include "m_misc.h"
#include "i_video.h"
#include "i_sound.h"

#include "d_net.h"
#include "g_game.h"

#ifdef __GNUG__
#pragma implementation "i_system.h"
#endif
#include "i_system.h"
#include "i_xpad.h"
#ifdef TTDOOM
#include "atari_vmode.h"
#endif
#include "m_argv.h"
#include "atari_sfp004.h"

void atari_enable_megaste_turbo(void);
void atari_restore_megaste_turbo(void);

static long old_super_stack = 0;

#ifdef TTDOOM
#include <mint/cookie.h>

// The 68030's caches (CACR). TOS 3.06 boots with $3111 (both caches, burst
// fills, write allocate), but the desktop's Cache option can leave them off,
// which would cost far more than anything here saves. Write allocate is for
// code in user mode, and the game runs in supervisor mode from here on:
// without it, the renderer's byte writes that miss the data cache don't
// evict colormap and texture lines (0.5% under Hatari). Both caches are
// cleared at each change, so nothing cached in user mode goes stale.
// -cacr <hex> sets another value (bits $3111) for measuring on a real TT:
// Hatari charges 2 clocks a long for the rest of a burst fill, where a
// 68030 can take 1, and runs 10% faster with data bursts off (0111), which
// a real TT-RAM may well not. I_Quit / I_Error put the old value back.
#define CACR_EI		0x0001	// instruction cache on
#define CACR_CI		0x0008	// clear the instruction cache
#define CACR_IBE	0x0010	// instruction burst fills
#define CACR_ED		0x0100	// data cache on
#define CACR_CD		0x0800	// clear the data cache
#define CACR_DBE	0x1000	// data burst fills
#define CACR_WA		0x2000	// write allocate

static long	old_cacr = -1;

static long I_GetCacr (void)
{
    long	v;

    __asm__ volatile ("movec	%%cacr,%0" : "=d" (v));
    return v;
}

static void I_SetCacr (long v)
{
    __asm__ volatile ("movec	%0,%%cacr" : : "d" (v) : "memory");
}

static void I_InitCaches (void)
{
    long	cpu, cacr;
    int		p;

    if (Getcookie (C__CPU, &cpu) != C_FOUND || cpu != 30)
	return;
    old_cacr = I_GetCacr ();
    cacr = CACR_EI | CACR_IBE | CACR_ED | CACR_DBE;
    p = M_CheckParm ("-cacr");
    if (p && p < myargc - 1)
	cacr = strtol (myargv[p+1], NULL, 16) & 0x3111;
    // Enabling a cache that was off: clear it first
    I_SetCacr (cacr | CACR_CI | CACR_CD);
    printf ("I_Init: 68030 caches %04lx (were %04lx)\n", cacr, old_cacr);
}

static void I_RestoreCaches (void)
{
    if (old_cacr >= 0)
	I_SetCacr (old_cacr | CACR_CI | CACR_CD);
    old_cacr = -1;
}
#else
#define I_InitCaches()
#define I_RestoreCaches()
#endif
static int super_enabled = 0;



int	ram_used = 39*1024*1025/16;


void
I_Tactile
( int	on,
  int	off,
  int	total )
{
  // UNUSED.
  on = off = total = 0;
}

ticcmd_t	emptycmd;
ticcmd_t*	I_BaseTiccmd(void)
{
    return &emptycmd;
}


int  I_GetHeapSize (void)
{
    return ram_used;
}

#ifdef TTDOOM
// What is allocated outside the zone after it: the WAD directory (about
// 50K for the registered DOOM.WAD), the DMA sound buffers, stdio.
#define ZONE_SPARE	(128*1024L)
#endif

static void *I_AllocAligned (long length);

byte* I_ZoneBase (int*	size)
{
    *size = ram_used;
#ifdef TTDOOM
    // The display pages first, in ST-RAM: on a 4 MB TT or Falcon the zone
    // would otherwise leave no room for them. Then the zone takes what is
    // free, up to ram_used, less ZONE_SPARE. With TT-RAM it is all of
    // ram_used there, as before.
    atari_vmode_reserve();
    {
	long	st = Mxalloc(-1, 0);
	long	alt = Mxalloc(-1, 1);
	long	avail = (alt > st ? alt : st) - ZONE_SPARE;

	if (avail < *size)
	    *size = avail;
    }
#endif
    unsigned long memavail = Malloc(-1);
    printf("Memory available: %ld\n", memavail);
    printf("Trying to allocate %d bytes\n", *size);
    unsigned long addr = (unsigned long) I_AllocAligned (*size);
    if (!addr) {
        I_Error("Not enough memory available");
    } else if (addr >= (1L<<24)) {
        printf("Fast RAM allocated at %08lx (program at %08lx)\n", addr,
               (unsigned long) _base->p_tbase);
    } else if (addr >= (1L<<22)) {
        printf("Alt-RAM allocated at %06lx\n", addr);
    } else {
        printf("ST-RAM allocated at %06lx\n", addr);
    }
    return (byte*)addr;
}



//
// I_GetTime
// returns time in 1/70th second tics
//
int  I_GetTime (void)
{
    static unsigned long basetime=0;
    unsigned long t = *_hz_200;
    if (!basetime)
	basetime = t;
    return (t-basetime)*TICRATE/200;
}



//
// I_Init
//
void I_Init (void)
{
    long super_ret;
    // Xpad's discovery uses Supexec: before entering supervisor mode
    I_InitXpad();
    printf("I_Init: Enabling supervisor mode.\n");
    super_ret = Super(0L);
    old_super_stack = super_ret;
    super_enabled = 1;
    /* Arm SFP-004 68882 FixedDiv dispatch (on by default; validated on hardware).
     * Now in supervisor (and staying) so the CIR is reachable. -nofpu forces the
     * software FixedDiv, e.g. for A/B framerate comparison via -timedemo.
     * R_Init/P_Init ran earlier in user mode and correctly used the software path. */
    if (!M_CheckParm("-nofpu"))
        sfp004_arm();
    if (!M_CheckParm("-nomste16"))
        atari_enable_megaste_turbo();
    I_InitCaches();
    I_InitSound();
    I_InitMusic();
    //  I_InitGraphics();
}

//
// I_Quit
//
void I_Quit (void)
{
    D_QuitNetGame ();
    I_ShutdownSound();
    I_ShutdownMusic();
    M_SaveDefaults ();
    I_ShutdownGraphics();
    atari_restore_megaste_turbo();
    I_RestoreCaches();
    if (super_enabled && old_super_stack)
        Super((void *)old_super_stack);
    exit(0);
}

void I_WaitVBL(int count)
{
#ifdef SGI
    sginap(1);                                           
#else
#ifdef SUN
    sleep(0);
#else
    usleep (count * (1000000/70) );                                
#endif
#endif
}

void I_BeginRead(void)
{
}

void I_EndRead(void)
{
}

// malloc on a 16-byte boundary (never freed). GEMDOS only keeps blocks
// even, so the zone and screens could start 2 bytes off a long: then every
// long a 68030 reads from them takes two bus cycles, and whether they did
// depended on what was allocated before (TTDOOM's timedemos ran 2-4%
// slower from one launcher than another).
static void *I_AllocAligned (long length)
{
    unsigned long mem = (unsigned long) malloc (length + 15);

    return mem ? (void *) ((mem + 15) & ~15UL) : NULL;
}

byte*	I_AllocLow(int length)
{
    byte*	mem;

    mem = (byte *)I_AllocAligned (length);
    memset (mem,0,length);
    return mem;
}


//
// I_Error
//
extern boolean demorecording;

void I_Error (char *error, ...)
{
    va_list	argptr;

    // Message first.
    va_start (argptr,error);
    fprintf (stderr, "Error: ");
    vfprintf (stderr,error,argptr);
    fprintf (stderr, "\n");
    va_end (argptr);

    fflush( stderr );

    // Shutdown. Here might be other errors.
    if (demorecording)
	G_CheckDemoStatus();

    D_QuitNetGame ();
    // Give the VBL back to TOS and stop DMA sound: left installed, the
    // sound VBL runs on in the freed program and hangs the machine once
    // the next program loads over it (as after every timedemo)
    I_ShutdownSound();
    I_ShutdownGraphics();
    atari_restore_megaste_turbo();
    I_RestoreCaches();
    if (super_enabled && old_super_stack)
        Super((void *)old_super_stack);
    getchar();
    exit(-1);
}
