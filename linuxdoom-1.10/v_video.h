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
//	Gamma correction LUT.
//	Functions to draw patches (by post) directly to screen.
//	Functions to blit a block to the screen.
//
//-----------------------------------------------------------------------------


#ifndef __V_VIDEO__
#define __V_VIDEO__

#include "doomtype.h"

#include "doomdef.h"

// Needed because we are refering to patches.
#include "r_data.h"

//
// VIDEO
//

#define CENTERY			(SCREENHEIGHT/2)

// 2D artwork (title pages, menus, intermission, finale) is authored for a
// 320x200 screen, which DOS showed on a 4:3 monitor (as the ST does). On the
// taller TTDOOM screen (square pixels) it is stretched to the full height:
// the drawers for those screens set v_page, and V_DrawPatch then takes y in
// 200-line page coordinates and draws screen row d from page row
// d*ORIGHEIGHT/SCREENHEIGHT. No effect at SCREENHEIGHT 200.
#define ORIGHEIGHT		200

extern int	v_page;

#if SCREENHEIGHT > ORIGHEIGHT
// One column of a patch at screen column x, its top at page row y (page
// coordinates, stretched), into screens[scrn].
void V_DrawPageColumn (int x, int y, int scrn, column_t *column);
#endif


// Screen 0 is the screen updated by I_Update screen.
// Screen 1 is an extra buffer.



extern	byte*		screens[5];

extern  int	dirtybox[4];
#ifdef TTDOOM
// What was marked above the status bar since I_FinishUpdate last took it
extern  int	playdirtybox[4];
#endif

extern	byte	gammatable[5][256];
extern	int	usegamma;



// Allocates buffer screens, call before R_Init.
void V_Init (void);


void
V_CopyRect
( int		srcx,
  int		srcy,
  int		srcscrn,
  int		width,
  int		height,
  int		destx,
  int		desty,
  int		destscrn );

void
V_DrawPatch
( int		x,
  int		y,
  int		scrn,
  patch_t*	patch);

void
V_DrawPatchDirect
( int		x,
  int		y,
  int		scrn,
  patch_t*	patch );


// Draw a linear block of pixels into the view buffer.
void
V_DrawBlock
( int		x,
  int		y,
  int		scrn,
  int		width,
  int		height,
  byte*		src );

// Reads a linear block of pixels into the view buffer.
void
V_GetBlock
( int		x,
  int		y,
  int		scrn,
  int		width,
  int		height,
  byte*		dest );


void
V_MarkRect
( int		x,
  int		y,
  int		width,
  int		height );

void
V_ClearRect();

#endif
//-----------------------------------------------------------------------------
//
// $Log:$
//
//-----------------------------------------------------------------------------
