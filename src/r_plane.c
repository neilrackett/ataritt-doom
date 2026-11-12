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
//	Here is a core component: drawing the floors and ceilings,
//	 while maintaining a per column clipping list only.
//	Moreover, the sky areas have to be determined.
//
//-----------------------------------------------------------------------------


static const char
rcsid[] = "$Id: r_plane.c,v 1.4 1997/02/03 16:47:55 b1 Exp $";

#include <stdlib.h>

#include "i_system.h"
#include "z_zone.h"
#include "w_wad.h"

#include "doomdef.h"
#include "doomstat.h"

#include "r_local.h"
#include "r_sky.h"



planefunction_t		floorfunc;
planefunction_t		ceilingfunc;

//
// opening
//

// Here comes the obnoxious "visplane".
#define MAXVISPLANES	128
visplane_t		visplanes[MAXVISPLANES];
visplane_t*		lastvisplane;
visplane_t*		floorplane;
visplane_t*		ceilingplane;

// ?
#define MAXOPENINGS	SCREENWIDTH*64
short			openings[MAXOPENINGS];
short*			lastopening;


//
// Clip values are the solid pixel bounding the range.
//  floorclip starts out SCREENHEIGHT
//  ceilingclip starts out -1
//
short			floorclip[SCREENWIDTH];
short			ceilingclip[SCREENWIDTH];

//
// spanstart holds the start of a plane span
// initialized to 0 at start
//
int			spanstart[SCREENHEIGHT];
int			spanstop[SCREENHEIGHT];

//
// texture mapping
//
lighttable_t**		planezlight;
fixed_t			planeheight;

fixed_t			yslope[SCREENHEIGHT];
fixed_t			distscale[SCREENWIDTH];
fixed_t			basexscale;
fixed_t			baseyscale;

fixed_t			cachedheight[SCREENHEIGHT];
fixed_t			cacheddistance[SCREENHEIGHT];
fixed_t			cachedxstep[SCREENHEIGHT];
fixed_t			cachedystep[SCREENHEIGHT];



//
// R_InitPlanes
// Only at game startup.
//
void R_InitPlanes (void)
{
  // Doh!
}


//
// R_MapPlane
//
// Uses global vars:
//  planeheight
//  ds_source
//  basexscale
//  baseyscale
//  viewx
//  viewy
//
// BASIC PRIMITIVE
//
#if defined(USEASM) && defined(__M68020__)
// 68020+ (TTDOOM): a visplane's spans are set up as records (R_MapPlane)
// and drawn together after it (R_FlushSpans), as walls do their columns.
#define MAXPLANESPANS	256
static spanrec_t	planespans[MAXPLANESPANS];
static spanrec_t*	planespan = planespans;

static void R_FlushSpans (void)
{
    if (detailshift)
	R_DrawSpansLow (planespans, planespan, ds_source);
    else
	R_DrawSpans (planespans, planespan, ds_source);
    planespan = planespans;
}

// R_MakeSpans only notes each span (R_StageSpan), and R_MapStagedSpans
// works them out afterwards: R_MapPlane's sums and R_MakeSpans' loop in
// turn missed the 68030's instruction cache a third of the time.
#define MAXSTAGEDSPANS	512
typedef struct
{
    short	y;
    short	x1;
    short	x2;
} stagedspan_t;
static stagedspan_t	stagedspans[MAXSTAGEDSPANS];
static stagedspan_t*	stagedspan = stagedspans;

static void R_MapStagedSpans (void);

static inline void R_StageSpan (int y, int x1, int x2)
{
    stagedspan->y = y;
    stagedspan->x1 = x1;
    stagedspan->x2 = x2;
    if (++stagedspan == stagedspans + MAXSTAGEDSPANS)
	R_MapStagedSpans ();
}
#endif

// Static, so that R_MakeSpans and R_DrawPlanes call it and each other
// without the stack arguments of a global call: R_MakeSpans runs for every
// column of every visplane.
#if defined(USEASM) && defined(__M68020__)
static inline
#else
static
#endif
void
R_MapPlane
( int		y,
  int		x1,
  int		x2 )
{
    angle_t	angle;
    fixed_t	distance;
    fixed_t	length;
    unsigned	index;
	
#ifdef RANGECHECK
    if (x2 < x1
	|| x1<0
	|| x2>=viewwidth
	|| (unsigned)y>viewheight)
    {
	I_Error ("R_MapPlane: %i, %i at %i",x1,x2,y);
    }
#endif

    if (planeheight != cachedheight[y])
    {
	cachedheight[y] = planeheight;
	distance = cacheddistance[y] = FixedMul (planeheight, yslope[y]);
	ds_xstep = cachedxstep[y] = FixedMul (distance,basexscale);
	ds_ystep = cachedystep[y] = FixedMul (distance,baseyscale);
    }
    else
    {
	distance = cacheddistance[y];
	ds_xstep = cachedxstep[y];
	ds_ystep = cachedystep[y];
    }
	
    length = FixedMul (distance,distscale[x1]);
    angle = (viewangle + xtoviewangle[x1])>>ANGLETOFINESHIFT;
    ds_xfrac = viewx + FixedScale32(length, finecosine[angle]);
    ds_yfrac = -viewy - FixedScale32(length, finesine[angle]);

    if (fixedcolormap)
	ds_colormap = fixedcolormap;
    else
    {
	index = distance >> LIGHTZSHIFT;
	
	if (index >= MAXLIGHTZ )
	    index = MAXLIGHTZ-1;

	ds_colormap = planezlight[index];
    }
	
#if defined(USEASM) && defined(__M68020__)
    R_SetupSpan (planespan, y, x1, x2, ds_colormap,
		 ds_xfrac, ds_yfrac, ds_xstep, ds_ystep, detailshift);
    if (++planespan == planespans + MAXPLANESPANS)
	R_FlushSpans ();
#else
    ds_y = y;
    ds_x1 = x1;
    ds_x2 = x2;

    // high or low detail
    spanfunc ();	
#endif
}


#if defined(USEASM) && defined(__M68020__)
// A view row's distance and texture steps for the plane height they were
// last worked out for (id's cachedheight[] and so on, a row together in
// one line of the data cache)
typedef struct
{
    fixed_t	height;
    fixed_t	distance;
    fixed_t	xstep;
    fixed_t	ystep;
} planerow_t;

static planerow_t	planerows[SCREENHEIGHT];

// The spans R_MakeSpans noted, set up and drawn: R_MapPlane for each, with
// the plane's and the view's values in locals and nothing through the ds_
// globals (gcc wrote each of those to memory and read it back, a span).
// An instance for each detail level, and for FixedMul as the 64-bit
// multiply or (a 68060) the call.
#ifdef FIXEDMUL_MULS64
#define SPAN_MUL(a, b) (muls64 ? FixedMul64 ((a), (b)) : FixedMulSplit ((a), (b)))
#else
#define SPAN_MUL(a, b) FixedMul ((a), (b))
#endif

static inline __attribute__((always_inline)) void
R_MapStagedSpansAt (const int low, const int muls64)
{
    const stagedspan_t*	t;
    const stagedspan_t* const end = stagedspan;
    spanrec_t*		r = planespan;
    const fixed_t	height = planeheight;
    const fixed_t	xscale = basexscale;
    const fixed_t	yscale = baseyscale;
    const fixed_t	vx = viewx;
    const fixed_t	vy = -viewy;
    const angle_t	va = viewangle;
    lighttable_t* const	fixedmap = fixedcolormap;
    lighttable_t** const zlight = planezlight;

    // The rows these spans are on, at this plane's height: a loop of its
    // own, so that the spans' loop is small enough for the instruction
    // cache
    for (t = stagedspans; t < end; t++)
    {
	const int	y = t->y;
	planerow_t* const row = &planerows[y];

	if (height != row->height)
	{
	    fixed_t	distance;

	    row->height = height;
	    distance = row->distance = SPAN_MUL (height, yslope[y]);
	    row->xstep = SPAN_MUL (distance, xscale);
	    row->ystep = SPAN_MUL (distance, yscale);
	}
    }

    for (t = stagedspans; t < end; t++)
    {
	const int	y = t->y;
	const int	x1 = t->x1;
	const planerow_t* const row = &planerows[y];
	const fixed_t	distance = row->distance;
	angle_t		angle;
	fixed_t		length;
	fixed_t		xfrac;
	fixed_t		yfrac;
	unsigned	index;
	lighttable_t*	colormap;

	length = SPAN_MUL (distance, distscale[x1]);
	angle = (va + xtoviewangle[x1])>>ANGLETOFINESHIFT;
	xfrac = vx + SPAN_MUL (length, finecosine[angle]);
	yfrac = vy - SPAN_MUL (length, finesine[angle]);

	// (the light table first, then the fixed one over it: gcc put the
	// table look-up, the usual case, far out of the loop otherwise)
	index = distance >> LIGHTZSHIFT;
	
	if (index >= MAXLIGHTZ )
	    index = MAXLIGHTZ-1;

	colormap = zlight[index];
	if (__builtin_expect (fixedmap != NULL, 0))
	    colormap = fixedmap;

	R_SetupSpan (r, y, x1, t->x2, colormap, xfrac, yfrac,
		     row->xstep, row->ystep, low);
	if (++r == planespans + MAXPLANESPANS)
	{
	    planespan = r;
	    R_FlushSpans ();
	    r = planespans;
	}
    }
    planespan = r;
    stagedspan = stagedspans;
    R_FlushSpans ();
}

static __attribute__((noinline)) void R_MapStagedSpans (void)
{
#ifdef FIXEDMUL_MULS64
    if (fixedmul_muls64)
    {
	if (detailshift)
	    R_MapStagedSpansAt (1, 1);
	else
	    R_MapStagedSpansAt (0, 1);
	return;
    }
#endif
    if (detailshift)
	R_MapStagedSpansAt (1, 0);
    else
	R_MapStagedSpansAt (0, 0);
}
#endif

//
// R_ClearPlanes
// At begining of frame.
//
void R_ClearPlanes (void)
{
    int		i;
    angle_t	angle;
    
    // opening / clipping determination
    for (i=0 ; i<viewwidth ; i++)
    {
	floorclip[i] = viewheight;
	ceilingclip[i] = -1;
    }

    lastvisplane = visplanes;
    lastopening = openings;
    
    // texture calculation
#if defined(USEASM) && defined(__M68020__)
    for (i=0 ; i<SCREENHEIGHT ; i++)
	planerows[i].height = 0;
#else
    memset (cachedheight, 0, sizeof(cachedheight));
#endif

    // left to right mapping
    angle = (viewangle-ANG90)>>ANGLETOFINESHIFT;
	
    // scale will be unit scale at SCREENWIDTH/2 distance
    basexscale = FixedDiv (finecosine[angle],centerxfrac);
    baseyscale = -FixedDiv (finesine[angle],centerxfrac);
}




//
// R_FindPlane
//
visplane_t*
R_FindPlane
( fixed_t	height,
  int		picnum,
  int		lightlevel )
{
    visplane_t*	check;
	
    if (picnum == skyflatnum)
    {
	height = 0;			// all skys map together
	lightlevel = 0;
    }
	
    for (check=visplanes; check<lastvisplane; check++)
    {
	if (height == check->height
	    && picnum == check->picnum
	    && lightlevel == check->lightlevel)
	{
	    break;
	}
    }
    
			
    if (check < lastvisplane)
	return check;
		
    if (lastvisplane - visplanes == MAXVISPLANES)
	I_Error ("R_FindPlane: no more visplanes");
		
    lastvisplane++;

    check->height = height;
    check->picnum = picnum;
    check->lightlevel = lightlevel;
    check->minx = SCREENWIDTH;
    check->maxx = -1;
    
    memset (check->top,0xff,sizeof(check->top));
		
    return check;
}


//
// R_CheckPlane
//
visplane_t*
R_CheckPlane
( visplane_t*	pl,
  int		start,
  int		stop )
{
    int		intrl;
    int		intrh;
    int		unionl;
    int		unionh;
    int		x;
	
    if (start < pl->minx)
    {
	intrl = pl->minx;
	unionl = start;
    }
    else
    {
	unionl = pl->minx;
	intrl = start;
    }
	
    if (stop > pl->maxx)
    {
	intrh = pl->maxx;
	unionh = stop;
    }
    else
    {
	unionh = pl->maxx;
	intrh = stop;
    }

    for (x=intrl ; x<= intrh ; x++)
	if (pl->top[x] != 0xff)
	    break;

    if (x > intrh)
    {
	pl->minx = unionl;
	pl->maxx = unionh;

	// use the same one
	return pl;		
    }
	
    // make a new visplane
    lastvisplane->height = pl->height;
    lastvisplane->picnum = pl->picnum;
    lastvisplane->lightlevel = pl->lightlevel;
    
    pl = lastvisplane++;
    pl->minx = start;
    pl->maxx = stop;

    memset (pl->top,0xff,sizeof(pl->top));
		
    return pl;
}


//
// R_MakeSpans
//
static inline void
R_MakeSpans
( int		x,
  int		t1,
  int		b1,
  int		t2,
  int		b2 )
{
#if defined(USEASM) && defined(__M68020__)
#define R_MapPlane	R_StageSpan
#endif
    while (t1 < t2 && t1<=b1)
    {
	R_MapPlane (t1,spanstart[t1],x-1);
	t1++;
    }
    while (b1 > b2 && b1>=t1)
    {
	R_MapPlane (b1,spanstart[b1],x-1);
	b1--;
    }
#undef R_MapPlane
	
    while (t2 < t1 && t2<=b2)
    {
	spanstart[t2] = x;
	t2++;
    }
    while (b2 > b1 && b2>=t2)
    {
	spanstart[b2] = x;
	b2--;
    }
}



//
// R_DrawPlanes
// At the end of each frame.
//
void R_DrawPlanes (void)
{
    visplane_t*		pl;
    int			light;
    int			x;
    int			stop;
    int			angle;
				
#ifdef RANGECHECK
    if (ds_p - drawsegs > MAXDRAWSEGS)
	I_Error ("R_DrawPlanes: drawsegs overflow (%i)",
		 ds_p - drawsegs);
    
    if (lastvisplane - visplanes > MAXVISPLANES)
	I_Error ("R_DrawPlanes: visplane overflow (%i)",
		 lastvisplane - visplanes);
    
    if (lastopening - openings > MAXOPENINGS)
	I_Error ("R_DrawPlanes: opening overflow (%i)",
		 lastopening - openings);
#endif

    for (pl = visplanes ; pl < lastvisplane ; pl++)
    {
	if (pl->minx > pl->maxx)
	    continue;

	
	// sky flat
	if (pl->picnum == skyflatnum)
	{
	    dc_iscale = YASPECT_INV(pspriteiscale)>>detailshift;
	    
	    // Sky is allways drawn full bright,
	    //  i.e. colormaps[0] is used.
	    // Because of this hack, sky is not affected
	    //  by INVUL inverse mapping.
	    dc_colormap = colormaps;
	    dc_texturemid = skytexturemid;
	    for (x=pl->minx ; x <= pl->maxx ; x++)
	    {
		dc_yl = pl->top[x];
		dc_yh = pl->bottom[x];

		if (dc_yl <= dc_yh)
		{
		    angle = (viewangle + xtoviewangle[x])>>ANGLETOSKYSHIFT;
		    dc_x = x;
		    dc_source = R_GetColumn(skytexture, angle);
		    colfunc ();
		}
	    }
	    continue;
	}
	
	// regular flat
	ds_source = W_CacheLumpNum(firstflat +
				   flattranslation[pl->picnum],
				   PU_STATIC);
	
	planeheight = abs(pl->height-viewz);
	light = (pl->lightlevel >> LIGHTSEGSHIFT)+extralight;

	if (light >= LIGHTLEVELS)
	    light = LIGHTLEVELS-1;

	if (light < 0)
	    light = 0;

	planezlight = zlight[light];

	pl->top[pl->maxx+1] = 0xff;
	pl->top[pl->minx-1] = 0xff;
		
	stop = pl->maxx + 1;

	for (x=pl->minx ; x<= stop ; x++)
	{
	    R_MakeSpans(x,pl->top[x-1],
			pl->bottom[x-1],
			pl->top[x],
			pl->bottom[x]);
	}
#if defined(USEASM) && defined(__M68020__)
	R_MapStagedSpans ();
#endif
	
	Z_ChangeTag (ds_source, PU_CACHE);
    }
}
