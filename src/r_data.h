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
// DESCRIPTION:
//  Refresh module, data I/O, caching, retrieval of graphics
//  by name.
//
//-----------------------------------------------------------------------------


#ifndef __R_DATA__
#define __R_DATA__

#include "r_defs.h"
#include "r_state.h"
#include "z_zone.h"
#include "w_wad.h"

#ifdef __GNUG__
#pragma interface
#endif

// The texture tables (r_data.c)
extern int*		texturewidthmask;
extern short**		texturecolumnlump;
extern unsigned short**	texturecolumnofs;
extern byte**		texturecomposite;

// Retrieve column data for span blitting.
byte*
R_GetColumnSlow
( int		tex,
  int		col );

// R_GetColumnSlow, inline for a column that is already cached: walls ask
// for one each column tier they draw, and the call chain through
// W_CacheLumpNum cost more than the drawing setup. W_CacheLumpNum
// (lump, PU_CACHE) on a lump cached as PU_CACHE changes nothing, so that
// case returns the lump directly; anything else takes the full path.
// R_GetCachedColumn is that case alone, NULL otherwise: it never allocates,
// so it can't purge a lump another column still points into.
static inline byte* R_GetCachedColumn (int tex, int col)
{
    int		lump;
    int		ofs;
    byte*	p;

    col &= texturewidthmask[tex];
    lump = texturecolumnlump[tex][col];
    ofs = texturecolumnofs[tex][col];
    if (lump > 0)
    {
	p = (byte *)lumpcache[lump];
	if (p && ((memblock_t *)(p - sizeof(memblock_t)))->tag == PU_CACHE)
	    return p + ofs;
    }
    else if ((p = texturecomposite[tex]) != NULL)
	return p + ofs;
    return NULL;
}

static inline byte* R_GetColumn (int tex, int col)
{
    byte*	p = R_GetCachedColumn (tex, col);

    return p ? p : R_GetColumnSlow (tex, col);
}


// I/O, setting up the stuff.
void R_InitData (void);
void R_PrecacheLevel (void);


// Retrieval.
// Floor/ceiling opaque texture tiles,
// lookup by name. For animation?
int R_FlatNumForName (char* name);


// Called by P_Ticker for switches and animations,
// returns the texture number for the texture name.
int R_TextureNumForName (char *name);
int R_CheckTextureNumForName (char *name);

#endif
//-----------------------------------------------------------------------------
//
// $Log:$
//
//-----------------------------------------------------------------------------
