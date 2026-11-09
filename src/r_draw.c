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
//	The actual span/column drawing functions.
//	Here find the main potential for optimization,
//	 e.g. inline assembly, different algorithms.
//
//-----------------------------------------------------------------------------


static const char
rcsid[] = "$Id: r_draw.c,v 1.4 1997/02/03 16:47:55 b1 Exp $";


#include "doomdef.h"

#include "i_system.h"
#include "z_zone.h"
#include "w_wad.h"

#include "r_local.h"

// Needs access to LFB (guess what).
#include "v_video.h"

// State.
#include "doomstat.h"

#ifdef WORD_COLORMAPS
// The word colormap for a light table: every one the drawers are given
// points into colormaps
#define WORD_COLORMAP(cmap) \
    ((lighttable_t *)(colormaps_w + ((cmap) - colormaps)))
#endif


// ?
#define MAXWIDTH			SCREENWIDTH
#define MAXHEIGHT			SCREENHEIGHT

// status bar height at bottom of screen
#define SBARHEIGHT		32

//
// All drawing to the view buffer is accomplished in this file.
// The other refresh files only know about ccordinates,
//  not the architecture of the frame buffer.
// Conveniently, the frame buffer is a linear one,
//  and we need only the base address,
//  and the total size == width*height*depth/8.,
//


byte*		viewimage; 
int		viewwidth;
int		scaledviewwidth;
int		viewheight;
int		viewwindowx;
int		viewwindowy; 
byte*		ylookup[MAXHEIGHT]; 
int		columnofs[MAXWIDTH]; 

// Color tables for different players,
//  translate a limited part to another
//  (color ramps used for  suit colors).
//
byte		translations[3][256];	
 
 


//
// R_DrawColumn
// Source is the top of the column to scale.
//
lighttable_t*		dc_colormap; 
short			dc_x; 
short			dc_yl; 
short			dc_yh; 
fixed_t			dc_iscale; 
fixed_t			dc_texturemid;

// first pixel in a column (possibly virtual) 
byte*			dc_source;		

// just for profiling 
int			dccount;

/// @brief Converts a Q15.16 fixed-point value into a Q7.8 value that fits in a short
/// @param n the Q15.16 value to convert
/// @return a a short in Q7.8 format
static __attribute__((unused)) short reduce(fixed_t n)
{
    return (n >> 8) & 0xffff;
}

/// @brief Converts a Q7.8 value into an inverted 8.0Q6 representation where the integral part is after the fractional part.
/// @param n the Q7.8 value to convert
/// @return a short in 8.0Q6 format
static __attribute__((unused)) short to_8_0Q6(short n)
{
    return (n << 8) | ((n >> 8) & 0x007f);
}

//
// A column is a vertical slice/span from a wall texture that,
//  given the DOOM style restrictions on the view orientation,
//  will always have constant z depth.
// Thus a special case loop for very fast rendering can
//  be used. It has also been used with Wolfenstein 3D.
// 
#if defined(USEASM) && defined(__M68020__)
// 68020+ (TTDOOM): vanilla's 16.16 texture step in four instructions a
// pixel, from a column R_SetupColumn (r_draw.h) set up. pos keeps the
// fraction in the high word and the 7-bit texel row in the low word, which
// indexes the column directly. addx.l steps both halves at once: the
// fraction's carry out of bit 31 goes to X, and the next pixel's addx adds
// it to the row. Nothing else in the loop touches X, and X starts as the
// first step's carry with the fraction already one step on, so every row is
// vanilla's (frac >> 16) & 127 exactly. The row is kept to 7 bits (and.w),
// with the row step taken mod 128, so the low word never carries into the
// fraction. Low detail draws each texel to two pixels with one word store,
// from the word colormaps.
static inline __attribute__((always_inline)) void
R_DrawColumnRec (const colrec_t* c, const int low)
{
    unsigned	pos = c->pos;
    // The CCR to start with, and then each texel: below 32, so its high
    // bits are clear for the texel's use as an index
    unsigned	texel = (unsigned short)c->ccr;
    short	pairs = c->pairs;
    byte*	dest = c->dest;

    asm volatile (
	"move.w	%[texel],%%ccr			\n\t"
	"bne.s	1f				\n\t" // odd: the second
	"0:					\n\t"
	"move.b	(%[src],%[pos].w),%[texel]	\n\t"
	"addx.l	%[step],%[pos]			\n\t"
	"and.w	%[mask],%[pos]			\n\t"
	".if %c[low]				\n\t"
	"move.w	(%[cmap],%[texel].w*2),(%[dest])	\n\t"
	".else					\n\t"
	"move.b	(%[cmap],%[texel].w),(%[dest])	\n\t"
	".endif					\n\t"
	"1:					\n\t"
	"move.b	(%[src],%[pos].w),%[texel]	\n\t"
	"addx.l	%[step],%[pos]			\n\t"
	"and.w	%[mask],%[pos]			\n\t"
	".if %c[low]				\n\t"
	"move.w	(%[cmap],%[texel].w*2),%c[width](%[dest])	\n\t"
	".else					\n\t"
	"move.b	(%[cmap],%[texel].w),%c[width](%[dest])	\n\t"
	".endif					\n\t"
	"lea	(2*%c[width])(%[dest]),%[dest]	\n\t"
	"dbra	%[pairs],0b			\n\t"
	: [pos] "+d" (pos), [texel] "+d" (texel), [dest] "+a" (dest),
	  [pairs] "+d" (pairs)
	: [src] "a" (c->source), [cmap] "a" (c->cmap),
	  [step] "d" (c->step), [mask] "d" (127),
	  [width] "i" (SCREENWIDTH), [low] "i" (low)
	: "cc", "memory");
}

static inline __attribute__((always_inline)) void R_DrawColumnAt (const int low)
{
    colrec_t	c;

    if (dc_yh < dc_yl)
	return;
    R_SetupColumn (&c, dc_x, dc_yl, dc_yh, dc_source, dc_colormap,
		   dc_iscale, dc_texturemid, low);
    R_DrawColumnRec (&c, low);
}

void R_DrawColumns (const colrec_t* c, const colrec_t* end)
{
    for ( ; c < end; c++)
	R_DrawColumnRec (c, 0);
}

void R_DrawColumnsLow (const colrec_t* c, const colrec_t* end)
{
    for ( ; c < end; c++)
	R_DrawColumnRec (c, 1);
}
#else
// The column loop for R_DrawColumn and R_DrawColumnLow: low detail view
// columns are two screen columns wide, so it draws each pixel twice.
static inline __attribute__((always_inline)) void R_DrawColumnAt (const int low)
{ 
    short		count; 
    byte*		dest; 
    unsigned short	frac;
    unsigned short      fracstep;	 
 
    count = dc_yh - dc_yl; 

    // Zero length, column does not exceed a pixel.
    if (count < 0) 
	return; 
				 
    // After the zero-length test, which leaves the 68000 code as before
    short x = low ? dc_x << 1 : dc_x;

#ifdef RANGECHECK 
    if ((unsigned)x >= SCREENWIDTH
	|| dc_yl < 0
	|| dc_yh >= SCREENHEIGHT) 
	I_Error ("R_DrawColumn: %i to %i at %i", dc_yl, dc_yh, x); 
#endif 

    // Framebuffer destination address.
    // Use ylookup LUT to avoid multiply with ScreenWidth.
    // Use columnofs LUT for subwindows? 
    dest = ylookup[dc_yl] + columnofs[x];  

    // Determine scaling,
    //  which is the only mapping to be done.
    // Reduce to 8.8 fixed-point short.
    fracstep = reduce(dc_iscale);
    frac = reduce(dc_texturemid) + (int)(dc_yl-centery) * fracstep;

    // Inner loop that does the actual texture mapping,
    //  e.g. a DDA-lile scaling.
    // This is as fast as it gets.
    byte *source = dc_source;
    lighttable_t *colormap = dc_colormap;
#ifdef WORD_COLORMAPS
    if (low)
	colormap = WORD_COLORMAP (colormap);
#endif
#ifdef USEASM
    // Invert the order of the fixed-point values so that the integral part
    // is in the lowest 7 bits, thereby making bit-shifting unnecessary.
    fracstep = to_8_0Q6(fracstep);
    frac = to_8_0Q6(frac);
    // Invariant: only least-significant byte is ever != 0
    unsigned long tmp = 0;
    asm volatile (
        // Unroll loop 2x
        "lsr.w  #1,%[count]                     \n\t"

        // Even number of pixels to be drawn (i.e., Carry set)? Skip special initialization.
        "jcs    3f                              \n\t"
        
        // Odd number of pixels. Clear X flag and ompensate for %[dest]
        // getting incremented by 2*%[width] at end of loop.
        "add.w  %[tmp],%[frac]                  \n\t"
        "lea    (-1*%c[width])(%[dest]),%[dest] \n\t"
        "jmp    1f                              \n\t"

        // Even number of pixels. Clear X flag for next addx and enter loop.
        "3:                                     \n\t"
        "add.w  %[tmp],%[frac]                  \n\t"

        // Loop begin
        "0:                                     \n\t"

        "move.b %[frac],%[tmp]                  \n\t"
        "move.b (%[source],%[tmp].w),%[tmp]     \n\t"
        ".if %c[low]                            \n\t"
#ifdef WORD_COLORMAPS
        "move.w (%[colormap],%[tmp].w*2),(%[dest])\n\t"
#else
        "move.b (%[colormap],%[tmp].w),%[tmp]   \n\t"
        "move.b %[tmp],(%[dest])                \n\t"
        "move.b %[tmp],1(%[dest])               \n\t"
#endif
        ".else                                  \n\t"
        "move.b (%[colormap],%[tmp].w),(%[dest])\n\t"
        ".endif                                 \n\t"
        "addx.w %[step],%[frac]                 \n\t"
        "and.w  %[mask],%[frac]                 \n\t"

        "1:                                     \n\t"
        "move.b %[frac],%[tmp]                  \n\t"
        "move.b (%[source],%[tmp].w),%[tmp]     \n\t"
        ".if %c[low]                            \n\t"
#ifdef WORD_COLORMAPS
        "move.w (%[colormap],%[tmp].w*2),%c[width](%[dest])\n\t"
#else
        "move.b (%[colormap],%[tmp].w),%[tmp]   \n\t"
        "move.b %[tmp],%c[width](%[dest])       \n\t"
        "move.b %[tmp],(%c[width]+1)(%[dest])   \n\t"
#endif
        ".else                                  \n\t"
        "move.b (%[colormap],%[tmp].w),%c[width](%[dest])\n\t"
        ".endif                                 \n\t"
        "addx.w %[step],%[frac]                 \n\t"
        "and.w  %[mask],%[frac]                 \n\t"

        "lea    (2*%c[width])(%[dest]),%[dest]  \n\t"
        "dbra   %[count],0b                     \n\t"

        // outputs
        : [tmp] "+&d" (tmp)
        , [frac] "+&d" (frac)
        , [dest] "+&a" (dest)
        , [count] "+&d" (count)
        // inputs
        : [source] "a" (source)
        , [colormap] "a" (colormap)
        , [step] "d" (fracstep)
        , [zero] "d" (0)
        , [mask] "d" (0xff7f)
        , [width] "i" (SCREENWIDTH)
        , [low] "i" (low)
        // clobbers
        : "memory", "cc"
    );	
#else
    do 
    {
	// Re-map color indices from wall texture column
	//  using a lighting/special effects LUT.
	*dest = colormap[source[frac>>8]];
	if (low)
	    dest[1] = *dest;
	
	dest += SCREENWIDTH; 
	frac += fracstep;
	
    } while (count--); 
#endif
}
#endif

void R_DrawColumn (void) 
{ 
    R_DrawColumnAt (0);
} 

// Low detail: view columns are two screen columns wide
void R_DrawColumnLow (void) 
{ 
    R_DrawColumnAt (1);
} 



// UNUSED.
// Loop unrolled.
#if 0
void R_DrawColumn (void) 
{ 
    int			count; 
    byte*		source;
    byte*		dest;
    byte*		colormap;
    
    unsigned		frac;
    unsigned		fracstep;
    unsigned		fracstep2;
    unsigned		fracstep3;
    unsigned		fracstep4;	 
 
    count = dc_yh - dc_yl + 1; 

    source = dc_source;
    colormap = dc_colormap;		 
    dest = ylookup[dc_yl] + columnofs[dc_x];  
	 
    fracstep = dc_iscale<<9; 
    frac = (dc_texturemid + (dc_yl-centery)*dc_iscale)<<9; 
 
    fracstep2 = fracstep+fracstep;
    fracstep3 = fracstep2+fracstep;
    fracstep4 = fracstep3+fracstep;
	
    while (count >= 8) 
    { 
	dest[0] = colormap[source[frac>>25]]; 
	dest[SCREENWIDTH] = colormap[source[(frac+fracstep)>>25]]; 
	dest[SCREENWIDTH*2] = colormap[source[(frac+fracstep2)>>25]]; 
	dest[SCREENWIDTH*3] = colormap[source[(frac+fracstep3)>>25]];
	
	frac += fracstep4; 

	dest[SCREENWIDTH*4] = colormap[source[frac>>25]]; 
	dest[SCREENWIDTH*5] = colormap[source[(frac+fracstep)>>25]]; 
	dest[SCREENWIDTH*6] = colormap[source[(frac+fracstep2)>>25]]; 
	dest[SCREENWIDTH*7] = colormap[source[(frac+fracstep3)>>25]]; 

	frac += fracstep4; 
	dest += SCREENWIDTH*8; 
	count -= 8;
    } 
	
    while (count > 0)
    { 
	*dest = colormap[source[frac>>25]]; 
	dest += SCREENWIDTH; 
	frac += fracstep; 
	count--;
    } 
}
#endif





//
// Spectre/Invisibility.
//
#define FUZZTABLE		50 
#define FUZZOFF	(SCREENWIDTH)


short	fuzzoffset[FUZZTABLE] =
{
    FUZZOFF,-FUZZOFF,FUZZOFF,-FUZZOFF,FUZZOFF,FUZZOFF,-FUZZOFF,
    FUZZOFF,FUZZOFF,-FUZZOFF,FUZZOFF,FUZZOFF,FUZZOFF,-FUZZOFF,
    FUZZOFF,FUZZOFF,FUZZOFF,-FUZZOFF,-FUZZOFF,-FUZZOFF,-FUZZOFF,
    FUZZOFF,-FUZZOFF,-FUZZOFF,FUZZOFF,FUZZOFF,FUZZOFF,FUZZOFF,-FUZZOFF,
    FUZZOFF,-FUZZOFF,FUZZOFF,FUZZOFF,-FUZZOFF,-FUZZOFF,FUZZOFF,
    FUZZOFF,-FUZZOFF,-FUZZOFF,-FUZZOFF,-FUZZOFF,FUZZOFF,FUZZOFF,
    FUZZOFF,FUZZOFF,-FUZZOFF,FUZZOFF,FUZZOFF,-FUZZOFF,FUZZOFF 
}; 

short	fuzzpos = 0; 


//
// Framebuffer postprocessing.
// Creates a fuzzy image by copying pixels
//  from adjacent ones to left and right.
// Used with an all black colormap, this
//  could create the SHADOW effect,
//  i.e. spectres and invisible players.
//
#if defined(USEASM) && defined(__M68020__)
// 68020+ (TTDOOM): fuzzoffset repeated, so that a column of up to
// SCREENHEIGHT pixels starting anywhere in the table reads on without
// wrapping, and the drawer needs no test a pixel.
static short	fuzzoffsetx[FUZZTABLE + SCREENHEIGHT];

// R_DrawFuzzColumn and R_DrawFuzzColumnLow, in asm: five instructions a
// pixel (seven in low detail, where each of the two screen columns takes
// its own neighbour's pixel). The same pixels as the C loops.
static inline __attribute__((always_inline)) void R_DrawFuzzColumnAt (const int low)
{
    short		count;
    short		pos;
    byte*		dest;
    const short*	ofs;
    unsigned		texel = 0;	// high bits clear: an index
    short		o;

    // Adjust borders, as vanilla
    if (!dc_yl)
	dc_yl = 1;
    if (dc_yh == viewheight-1)
	dc_yh = viewheight - 2;
    count = dc_yh - dc_yl;
    if (count < 0)
	return;

    if (!fuzzoffsetx[0])
    {
	for (pos = 0; pos < FUZZTABLE + SCREENHEIGHT; pos++)
	    fuzzoffsetx[pos] = fuzzoffset[pos % FUZZTABLE];
    }
    dest = ylookup[dc_yl] + columnofs[low ? dc_x << 1 : dc_x];
    ofs = fuzzoffsetx + fuzzpos;
    // Use colormap #6 so the color gets a little darker.
    asm volatile (
	"0:					\n\t"
	"move.w	(%[ofs])+,%[o]			\n\t"
	"move.b	(%[dest],%[o].w),%[texel]	\n\t"
	"move.b	(%[cmap],%[texel].w),(%[dest])	\n\t"
	".if %c[low]				\n\t"
	"move.b	1(%[dest],%[o].w),%[texel]	\n\t"
	"move.b	(%[cmap],%[texel].w),1(%[dest])	\n\t"
	".endif					\n\t"
	"lea	%c[width](%[dest]),%[dest]	\n\t"
	"dbra	%[count],0b			\n\t"
	: [ofs] "+a" (ofs), [dest] "+a" (dest), [texel] "+d" (texel),
	  [o] "=&d" (o), [count] "+d" (count)
	: [cmap] "a" (colormaps + 6*256),
	  [width] "i" (SCREENWIDTH), [low] "i" (low)
	: "cc", "memory");
    pos = ofs - fuzzoffsetx;
    while (pos >= FUZZTABLE)
	pos -= FUZZTABLE;
    fuzzpos = pos;
}

void R_DrawFuzzColumn (void)
{
    R_DrawFuzzColumnAt (0);
}

void R_DrawFuzzColumnLow (void)
{
    R_DrawFuzzColumnAt (1);
}
#else
void R_DrawFuzzColumn (void) 
{ 
    short		count; 
    byte*		dest; 

    // Adjust borders. Low... 
    if (!dc_yl) 
	dc_yl = 1;

    // .. and high.
    if (dc_yh == viewheight-1) 
	dc_yh = viewheight - 2; 
		 
    count = dc_yh - dc_yl; 

    // Zero length.
    if (count < 0) 
	return; 

    
#ifdef RANGECHECK 
    if ((unsigned)dc_x >= SCREENWIDTH
	|| dc_yl < 0 || dc_yh >= SCREENHEIGHT)
    {
	I_Error ("R_DrawFuzzColumn: %i to %i at %i",
		 dc_yl, dc_yh, dc_x);
    }
#endif
    
    // Does not work with blocky mode.
    dest = ylookup[dc_yl] + columnofs[dc_x];
    short fuzzpos_tmp = fuzzpos; // Copy global into register for performance
    // Use colormap #6 so the color gets a little darker.
    byte *colormap = colormaps + 6*256;

#ifdef USEASM
    // The high-byte is never overwritten.
    unsigned short tmp = 0;
#endif

    do {
#ifdef USEASM
        asm (
                "move.b (%[dest],%[ofs].w),%[tmp]               \n\t"
                "move.b (%[colormap],%[tmp].w),%[tmp]           \n\t"
                // outputs
                : [tmp] "+d" (tmp)
                // inputs
                : [colormap] "a" (colormap)
                , [ofs] "d" (fuzzoffset[fuzzpos_tmp])
                , [dest] "a" (dest)
                // clobbers
                : "memory"
        );
        *dest = tmp;
#else
	// Lookup framebuffer, and retrieve
	//  a pixel that is either one column
	//  left or right of the current one.
	// Add index from colormap to index.
	*dest = colormap[dest[fuzzoffset[fuzzpos_tmp]]]; 
#endif

	// Clamp table lookup index.
	if (++fuzzpos_tmp == FUZZTABLE) 
	    fuzzpos_tmp = 0;

	dest += SCREENWIDTH; 
    } while (count--);

    fuzzpos = fuzzpos_tmp; // Copy register back into global
} 

// Low detail: R_DrawFuzzColumn two pixels wide, each pixel taking the fuzz
// from its own screen column.
void R_DrawFuzzColumnLow (void) 
{ 
    short	count; 
    byte*	dest; 
    byte*	colormap = colormaps + 6*256;

    // Adjust borders, as R_DrawFuzzColumn
    if (!dc_yl) 
	dc_yl = 1;
    if (dc_yh == viewheight-1) 
	dc_yh = viewheight - 2; 
    count = dc_yh - dc_yl; 
    if (count < 0) 
	return; 

    dest = ylookup[dc_yl] + columnofs[dc_x << 1];
    // fuzzpos in a register, as R_DrawFuzzColumn: the byte stores through
    // dest could alias the global, so gcc would reload it every pixel
    short pos = fuzzpos;
    do 
    {
	short ofs = fuzzoffset[pos];

	dest[0] = colormap[dest[ofs]];
	dest[1] = colormap[dest[1 + ofs]];
	if (++pos == FUZZTABLE) 
	    pos = 0;
	dest += SCREENWIDTH; 
    } while (count--);
    fuzzpos = pos;
} 
#endif
 
  
 

//
// R_DrawTranslatedColumn
// Used to draw player sprites
//  with the green colorramp mapped to others.
// Could be used with different translation
//  tables, e.g. the lighter colored version
//  of the BaronOfHell, the HellKnight, uses
//  identical sprites, kinda brightened up.
//
byte*	dc_translation;
byte*	translationtables;

void R_DrawTranslatedColumn (void) 
{ 
    short		count; 
    byte*		dest; 
    fixed_t		frac;
    fixed_t		fracstep;	 
 
    count = dc_yh - dc_yl; 
    if (count < 0) 
	return; 
				 
#ifdef RANGECHECK 
    if ((unsigned)dc_x >= SCREENWIDTH
	|| dc_yl < 0
	|| dc_yh >= SCREENHEIGHT)
    {
	I_Error ( "R_DrawColumn: %i to %i at %i",
		  dc_yl, dc_yh, dc_x);
    }
    
#endif 


    // WATCOM VGA specific.
    /* Keep for fixing.
    if (detailshift)
    {
	if (dc_x & 1)
	    outp (SC_INDEX+1,12); 
	else
	    outp (SC_INDEX+1,3);
	
	dest = destview + dc_yl*80 + (dc_x>>1); 
    }
    else
    {
	outp (SC_INDEX+1,1<<(dc_x&3)); 

	dest = destview + dc_yl*80 + (dc_x>>2); 
    }*/

    
    // FIXME. As above.
    dest = ylookup[dc_yl] + columnofs[dc_x]; 

    // Looks familiar.
    fracstep = dc_iscale; 
    frac = dc_texturemid + (dc_yl-centery)*fracstep; 

    // Here we do an additional index re-mapping.
    do 
    {
	// Translation tables are used
	//  to map certain colorramps to other ones,
	//  used with PLAY sprites.
	// Thus the "green" ramp of the player 0 sprite
	//  is mapped to gray, red, black/indigo. 
	*dest = dc_colormap[dc_translation[dc_source[frac>>FRACBITS]]];
	dest += SCREENWIDTH;
	
	frac += fracstep; 
    } while (count--); 
} 

// Low detail: R_DrawTranslatedColumn two pixels wide
void R_DrawTranslatedColumnLow (void) 
{ 
    short	count; 
    byte*	dest; 
    fixed_t	frac;
    fixed_t	fracstep;	 
 
    count = dc_yh - dc_yl; 
    if (count < 0) 
	return; 

    dest = ylookup[dc_yl] + columnofs[dc_x << 1]; 
    fracstep = dc_iscale; 
    frac = dc_texturemid + (dc_yl-centery)*fracstep; 
    do 
    {
	dest[0] = dest[1] = dc_colormap[dc_translation[dc_source[frac>>FRACBITS]]];
	dest += SCREENWIDTH;
	frac += fracstep; 
    } while (count--); 
} 




//
// R_InitTranslationTables
// Creates the translation tables to map
//  the green color ramp to gray, brown, red.
// Assumes a given structure of the PLAYPAL.
// Could be read from a lump instead.
//
void R_InitTranslationTables (void)
{
    short		i;
	
    translationtables = Z_Malloc (256*3+255, PU_STATIC, 0);
    translationtables = (byte *)(( (int)translationtables + 255 )& ~255);
    
    // translate just the 16 green colors
    for (i=0 ; i<256 ; i++)
    {
	if (i >= 0x70 && i<= 0x7f)
	{
	    // map green ramp to gray, brown, red
	    translationtables[i] = 0x60 + (i&0xf);
	    translationtables [i+256] = 0x40 + (i&0xf);
	    translationtables [i+512] = 0x20 + (i&0xf);
	}
	else
	{
	    // Keep all other colors as is.
	    translationtables[i] = translationtables[i+256] 
		= translationtables[i+512] = i;
	}
    }
}




//
// R_DrawSpan 
// With DOOM style restrictions on view orientation,
//  the floors and ceilings consist of horizontal slices
//  or spans with constant z depth.
// However, rotation around the world z axis is possible,
//  thus this mapping, while simpler and faster than
//  perspective correct texture mapping, has to traverse
//  the texture at an angle in all but a few cases.
// In consequence, flats are not stored by column (like walls),
//  and the inner loop has to step in texture space u and v.
//
short			ds_y; 
short			ds_x1; 
short			ds_x2;

lighttable_t*		ds_colormap; 

fixed_t			ds_xfrac; 
fixed_t			ds_yfrac; 
fixed_t			ds_xstep; 
fixed_t			ds_ystep;

// start of a 64*64 tile image 
byte*			ds_source;	

// just for profiling
int			dscount;

typedef unsigned int fixed_2in1_t;

// Poor man's SIMD:
// Pack xfrac and yfrac as 6.9 fixedpoint numbers into a single 32 bit integer:
// XXXXXXxx xxxxxxx0 YYYYYYyy yyyyyy0
static __attribute__((unused)) fixed_2in1_t pack(unsigned int a, unsigned int b)
{
    return ((a & 0x003fff80) << 10) | ((b & 0x003fff80) >> 6);
}

unsigned short rotl16 (unsigned short value, unsigned int count) {
    return (value << count) | (value >> (16 - count));
}

//
// Draws the actual span.
#if defined(USEASM) && defined(__M68020__)
// 68020+ (TTDOOM): 7.5 instructions a pixel (after Aki Laukkanen's span for
// ADoom). x (u) and y (v) are 16.16 as in vanilla, but split across two
// registers with the fractions crossed over (R_SetupSpan, r_draw.h):
//   u = v fraction : u texel (bits 5-0)
//   v = u fraction : v texel * 64 (bits 11-6)
// addx.l steps a register's fraction and texel together; the fraction's
// carry goes to X, and the other register's addx adds it to its own
// texel's low bit. So the two addx.l chain one X flag between u and v,
// nothing else in the loop touches X, and X starts as v's first carry (v's
// fraction is already one step on). or.w sets u's bits 15-6 and v's bits
// 15-12 and 5-0: v's carry ripples through bits 5-0 into its texel, and
// the and.w of the two is $f000 + v*64 + u, a texel of the flat 4096 bytes
// on (src is the flat + $1000).
// Steps are taken mod 64 per axis. Unlike vanilla, a row that wraps carries
// into the other fraction's lowest bit (1/65536 of a texel); vanilla's 16
// fraction bits otherwise, where the 68000 loop keeps 9. Low detail writes
// each texel to two pixels with one word store, from the word colormaps.
#define R_DRAWSPAN_PIXEL \
        "or.w    %%d6,%[u]                  \n\t" \
        "or.w    %%d7,%[v]                  \n\t" \
        "and.w   %[v],%[u]                  \n\t" /* $f000 + v*64 + u */ \
        "addx.l  %[vstep],%[v]              \n\t" \
        "move.b  (%[src],%[u].w),%%d5       \n\t" \
        "addx.l  %[ustep],%[u]              \n\t" \
        ".if %c[low]                        \n\t" \
        "move.w  (%[cmap],%%d5.w*2),(%[dest])+ \n\t" \
        ".else                              \n\t" \
        "move.b  (%[cmap],%%d5.w),(%[dest])+ \n\t" \
        ".endif                             \n\t"

static inline __attribute__((always_inline)) void
R_DrawSpanRec (const spanrec_t* r, const byte* source, const int low)
{
    unsigned	u = r->u;
    unsigned	v = r->v;
    byte*	dest = r->dest;
    short	pairs = r->pairs;
    // The CCR to start with, in d5, which then takes each texel: below 32,
    // so its high bits are already clear for the texel's use as an index
    register long ccr asm("d5") = (unsigned short)r->ccr;

    asm volatile (
        "move.w  #0xffc0,%%d6               \n\t"
        "move.w  #0xf03f,%%d7               \n\t"
        "move.w  %%d5,%%ccr                 \n\t"
        "beq.s   1f                         \n\t" // even: pairs only
        R_DRAWSPAN_PIXEL
        "1:                                 \n\t"
        "tst.w   %[pairs]                   \n\t"
        "bmi.s   3f                         \n\t"
        "2:                                 \n\t"
        R_DRAWSPAN_PIXEL
        R_DRAWSPAN_PIXEL
        "dbra    %[pairs],2b                \n\t"
        "3:                                 \n\t"
        : [dest] "+a"(dest), [u] "+d"(u), [v] "+d"(v), [pairs] "+d"(pairs),
          "+d"(ccr)
        : [src] "a"(source + 0x1000), [cmap] "a"(r->cmap),
          [ustep] "d"(r->ustep), [vstep] "d"(r->vstep),
          [low] "i"(low)
        : "d6", "d7", "cc", "memory");
}

static inline __attribute__((always_inline)) void R_DrawSpanAt (const int low)
{
    spanrec_t	r;

    R_SetupSpan (&r, ds_y, ds_x1, ds_x2, ds_colormap,
		 ds_xfrac, ds_yfrac, ds_xstep, ds_ystep, low);
    R_DrawSpanRec (&r, ds_source, low);
}

void R_DrawSpans (const spanrec_t* r, const spanrec_t* end, const byte* source)
{
    for ( ; r < end; r++)
	R_DrawSpanRec (r, source, 0);
}

void R_DrawSpansLow (const spanrec_t* r, const spanrec_t* end, const byte* source)
{
    for ( ; r < end; r++)
	R_DrawSpanRec (r, source, 1);
}

void R_DrawSpan (void)
{
    R_DrawSpanAt (0);
}

// Low detail: view columns are two screen columns wide
void R_DrawSpanLow (void)
{
    R_DrawSpanAt (1);
}
#else
void R_DrawSpan (void)
{
    byte*		dest;
    short		count;
    short		spot;
	 
#ifdef RANGECHECK 
    if (ds_x2 < ds_x1
	|| ds_x1<0
	|| ds_x2>=SCREENWIDTH  
	|| (unsigned)ds_y>SCREENHEIGHT)
    {
	I_Error( "R_DrawSpan: %i to %i at %i",
		 ds_x1,ds_x2,ds_y);
    }
//	dscount++; 
#endif 

    dest = ylookup[ds_y] + columnofs[ds_x1];

    // We do not check for zero spans here?
    count = ds_x2 - ds_x1; 

    lighttable_t *colormap = ds_colormap;
    byte *source = ds_source;
    // Poor man's SIMD:
    // Pack xfrac and yfrac as 6.9 fixedpoint numbers into a single 32 bit integer:
    // XXXXXXxx xxxxxxx0 YYYYYYyy yyyyyy0
    fixed_2in1_t mask = 0xfffefffe;
    fixed_2in1_t frac = pack(ds_xfrac, ds_yfrac);
    fixed_2in1_t step = pack(ds_xstep, ds_ystep);
    do 
    {
	// Current texture index in u,v.
	//spot = ((yfrac>>(16-6))&(63*64)) + ((xfrac>>16)&63);
        fixed_2in1_t frac2 = frac & 0xfc00fc00;
        spot = frac2 >> 16; // Implemented using swap
        spot = rotl16(spot, 6); // Want 6 bits of x in low position
        spot |= ((unsigned short)frac2) >> 4; // Add 6 bits of y in high position

	// Lookup pixel from flat texture tile,
	//  re-index using light/colormap.
	*dest++ = colormap[source[spot]];

	// Next step in u,v.
        frac += step;
        frac &= mask;

    } while (count--);
}
#endif



// UNUSED.
// Loop unrolled by 4.
#if 0
void R_DrawSpan (void) 
{ 
    unsigned	position, step;

    byte*	source;
    byte*	colormap;
    byte*	dest;
    
    unsigned	count;
    usingned	spot; 
    unsigned	value;
    unsigned	temp;
    unsigned	xtemp;
    unsigned	ytemp;
		
    position = ((ds_xfrac<<10)&0xffff0000) | ((ds_yfrac>>6)&0xffff);
    step = ((ds_xstep<<10)&0xffff0000) | ((ds_ystep>>6)&0xffff);
		
    source = ds_source;
    colormap = ds_colormap;
    dest = ylookup[ds_y] + columnofs[ds_x1];	 
    count = ds_x2 - ds_x1 + 1; 
	
    while (count >= 4) 
    { 
	ytemp = position>>4;
	ytemp = ytemp & 4032;
	xtemp = position>>26;
	spot = xtemp | ytemp;
	position += step;
	dest[0] = colormap[source[spot]]; 

	ytemp = position>>4;
	ytemp = ytemp & 4032;
	xtemp = position>>26;
	spot = xtemp | ytemp;
	position += step;
	dest[1] = colormap[source[spot]];
	
	ytemp = position>>4;
	ytemp = ytemp & 4032;
	xtemp = position>>26;
	spot = xtemp | ytemp;
	position += step;
	dest[2] = colormap[source[spot]];
	
	ytemp = position>>4;
	ytemp = ytemp & 4032;
	xtemp = position>>26;
	spot = xtemp | ytemp;
	position += step;
	dest[3] = colormap[source[spot]]; 
		
	count -= 4;
	dest += 4;
    } 
    while (count > 0) 
    { 
	ytemp = position>>4;
	ytemp = ytemp & 4032;
	xtemp = position>>26;
	spot = xtemp | ytemp;
	position += step;
	*dest++ = colormap[source[spot]]; 
	count--;
    } 
} 
#endif


//
// Again..
// (68000; 68020+ has the asm version beside R_DrawSpan.)
//
#if !(defined(USEASM) && defined(__M68020__))
void R_DrawSpanLow (void) 
{ 
    fixed_t		xfrac;
    fixed_t		yfrac; 
    byte*		dest; 
    short		count;
    short		spot; 
	 
#ifdef RANGECHECK 
    if (ds_x2 < ds_x1
	|| ds_x1<0
	|| ds_x2>=SCREENWIDTH  
	|| (unsigned)ds_y>SCREENHEIGHT)
    {
	I_Error( "R_DrawSpan: %i to %i at %i",
		 ds_x1,ds_x2,ds_y);
    }
//	dscount++; 
#endif 
	 
    xfrac = ds_xfrac; 
    yfrac = ds_yfrac; 

    // Blocky mode: each texel two screen columns wide. (id's version doubled
    // ds_x1 / ds_x2 before counting, so drew twice the span.)
    dest = ylookup[ds_y] + columnofs[ds_x1 << 1];
    count = ds_x2 - ds_x1; 
    do 
    { 
	spot = ((yfrac>>(16-6))&(63*64)) + ((xfrac>>16)&63);
	// Lowres/blocky mode does it twice,
	//  while scale is adjusted appropriately.
	*dest++ = ds_colormap[ds_source[spot]]; 
	*dest++ = ds_colormap[ds_source[spot]];
	
	xfrac += ds_xstep; 
	yfrac += ds_ystep; 

    } while (count--); 
}
#endif

//
// R_InitBuffer 
// Creats lookup tables that avoid
//  multiplies and other hazzles
//  for getting the framebuffer address
//  of a pixel to draw.
//
void
R_InitBuffer
( short		width,
  short		height ) 
{ 
    short		i; 

    // Handle resize,
    //  e.g. smaller view windows
    //  with border and/or status bar.
    viewwindowx = (SCREENWIDTH-width) >> 1; 

    // Column offset. For windows.
    for (i=0 ; i<width ; i++) 
	columnofs[i] = viewwindowx + i;

    // Samw with base row offset.
    if (width == SCREENWIDTH) 
	viewwindowy = 0; 
    else 
	viewwindowy = (SCREENHEIGHT-SBARHEIGHT-height) >> 1; 

    // Preclaculate all row offsets.
    for (i=0 ; i<height ; i++) 
	ylookup[i] = screens[0] + (i+viewwindowy)*SCREENWIDTH; 
} 
 
 


//
// R_FillBackScreen
// Fills the back screen with a pattern
//  for variable screen sizes
// Also draws a beveled edge.
//
void R_FillBackScreen (void) 
{ 
    byte*	src;
    byte*	dest; 
    int		x;
    int		y; 
    patch_t*	patch;

    // DOOM border patch.
    char	name1[] = "FLOOR7_2";

    // DOOM II border patch.
    char	name2[] = "GRNROCK";	

    char*	name;
	
    if (scaledviewwidth == 320)
	return;
	
    if ( gamemode == commercial)
	name = name2;
    else
	name = name1;
    
    src = W_CacheLumpName (name, PU_CACHE); 
    dest = screens[1]; 
	 
    for (y=0 ; y<SCREENHEIGHT-SBARHEIGHT ; y++) 
    { 
	for (x=0 ; x<SCREENWIDTH/64 ; x++) 
	{ 
	    memcpy (dest, src+((y&63)<<6), 64); 
	    dest += 64; 
	} 

	if (SCREENWIDTH&63) 
	{ 
	    memcpy (dest, src+((y&63)<<6), SCREENWIDTH&63); 
	    dest += (SCREENWIDTH&63); 
	} 
    } 
	
    patch = W_CacheLumpName ("brdr_t",PU_CACHE);

    for (x=0 ; x<scaledviewwidth ; x+=8)
	V_DrawPatch (viewwindowx+x,viewwindowy-8,1,patch);
    patch = W_CacheLumpName ("brdr_b",PU_CACHE);

    for (x=0 ; x<scaledviewwidth ; x+=8)
	V_DrawPatch (viewwindowx+x,viewwindowy+viewheight,1,patch);
    patch = W_CacheLumpName ("brdr_l",PU_CACHE);

    for (y=0 ; y<viewheight ; y+=8)
	V_DrawPatch (viewwindowx-8,viewwindowy+y,1,patch);
    patch = W_CacheLumpName ("brdr_r",PU_CACHE);

    for (y=0 ; y<viewheight ; y+=8)
	V_DrawPatch (viewwindowx+scaledviewwidth,viewwindowy+y,1,patch);


    // Draw beveled edge. 
    V_DrawPatch (viewwindowx-8,
		 viewwindowy-8,
		 1,
		 W_CacheLumpName ("brdr_tl",PU_CACHE));
    
    V_DrawPatch (viewwindowx+scaledviewwidth,
		 viewwindowy-8,
		 1,
		 W_CacheLumpName ("brdr_tr",PU_CACHE));
    
    V_DrawPatch (viewwindowx-8,
		 viewwindowy+viewheight,
		 1,
		 W_CacheLumpName ("brdr_bl",PU_CACHE));
    
    V_DrawPatch (viewwindowx+scaledviewwidth,
		 viewwindowy+viewheight,
		 1,
		 W_CacheLumpName ("brdr_br",PU_CACHE));
} 
 

//
// Copy a screen buffer.
//
void
R_VideoErase
( unsigned	ofs,
  int		count ) 
{ 
  // LFB copy.
  // This might not be a good idea if memcpy
  //  is not optiomal, e.g. byte by byte on
  //  a 32bit CPU, as GNU GCC/Linux libc did
  //  at one point.
    memcpy (screens[0]+ofs, screens[1]+ofs, count); 
} 


//
// R_DrawViewBorder
// Draws the border around the view
//  for different size windows?
//
void
V_MarkRect
( int		x,
  int		y,
  int		width,
  int		height ); 
 
void R_DrawViewBorder (void) 
{ 
    int		top;
    int		side;
    int		ofs;
    int		i; 
 
    if (scaledviewwidth == SCREENWIDTH) 
	return; 
  
    top = ((SCREENHEIGHT-SBARHEIGHT)-viewheight)/2; 
    side = (SCREENWIDTH-scaledviewwidth)/2; 
 
    // copy top and one line of left side 
    R_VideoErase (0, top*SCREENWIDTH+side); 
 
    // copy one line of right side and bottom 
    ofs = (viewheight+top)*SCREENWIDTH-side; 
    R_VideoErase (ofs, top*SCREENWIDTH+side); 
 
    // copy sides using wraparound 
    ofs = top*SCREENWIDTH + SCREENWIDTH-side; 
    side <<= 1;
    
    for (i=1 ; i<viewheight ; i++) 
    { 
	R_VideoErase (ofs, side); 
	ofs += SCREENWIDTH; 
    } 

    // ? 
    V_MarkRect (0,0,SCREENWIDTH, SCREENHEIGHT-SBARHEIGHT); 
} 
 
 
