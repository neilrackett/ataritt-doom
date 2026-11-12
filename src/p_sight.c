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
//	LineOfSight/Visibility checks, uses REJECT Lookup Table.
//
//-----------------------------------------------------------------------------

static const char
rcsid[] = "$Id: p_sight.c,v 1.3 1997/01/28 22:08:28 b1 Exp $";

#include <stdint.h>
#include "doomdef.h"

#include "i_system.h"
#include "p_local.h"

// State.
#include "r_state.h"
#include "asmsym.h"

//
// P_CheckSight
//
fixed_t		sightzstart;		// eye z of looker
fixed_t		topslope;
fixed_t		bottomslope;		// slopes to top and bottom of target

divline_t	strace;			// from t1 to t2
fixed_t		t2x;
fixed_t		t2y;

int		sightcounts[2];

static int32_t muls(int16_t a, int16_t b) {
#ifdef USEASM
    register int32_t result = a;
    asm (
        "muls.w     %[b],%[result]  \n\t"
        : [result] "+d" (result)
        : [b] "d" (b)
        : "cc"
    );
    return result;
#else
    return (int32_t)a * b;
#endif
}

//
// P_DivlineSide
// Returns side 0 (front), 1 (back), or 2 (on).
//
// Inline: the sight check's BSP walk calls it at every node and twice for
// every line it meets.
static inline int
P_DivlineSide
( fixed_t	x,
  fixed_t	y,
  divline_t*	node )
{
    fixed_t	dx;
    fixed_t	dy;
    fixed_t	left;
    fixed_t	right;

    if (!node->dx)
    {
	if (x==node->x)
	    return 2;
	
	if (x <= node->x)
	    return node->dy > 0;

	return node->dy < 0;
    }
    
    if (!node->dy)
    {
	if (x==node->y)
	    return 2;

	if (y <= node->y)
	    return node->dx < 0;

	return node->dx > 0;
    }
	
    dx = (x - node->x);
    dy = (y - node->y);

    left =  muls(node->dy>>FRACBITS, dx>>FRACBITS);
    right = muls(dy>>FRACBITS, node->dx>>FRACBITS);
	
    if (right < left)
	return 0;	// front side
    
    if (left == right)
	return 2;
    return 1;		// back side
}


//
// P_InterceptVector2
// Returns the fractional intercept point
// along the first divline.
// This is only called by the addthings and addlines traversers.
//
fixed_t
P_InterceptVector2
( divline_t*	v2,
  divline_t*	v1 )
{
    fixed_t	frac;
    fixed_t	num;
    fixed_t	den;
	
    den = FixedMul (v1->dy>>8,v2->dx) - FixedMul(v1->dx>>8,v2->dy);

    if (den == 0)
	return 0;
    //	I_Error ("P_InterceptVector: parallel");
    
    num = FixedMul ( (v1->x - v2->x)>>8 ,v1->dy) + 
	FixedMul ( (v2->y - v1->y)>>8 , v1->dx);
    frac = FixedDiv (num , den);

    return frac;
}

//
// P_NodeSide
// P_DivlineSide for a BSP node, whose line is in whole map units
// (r_defs.h): the same tests on the same values, the node's being id's
// fixed_t >> FRACBITS.
//
static inline int
P_NodeSide
( fixed_t	x,
  fixed_t	y,
  const node_t*	node )
{
    fixed_t	dx;
    fixed_t	dy;
    fixed_t	left;
    fixed_t	right;

    if (!node->dx)
    {
	if (x==((fixed_t)node->x << FRACBITS))
	    return 2;
	
	if (x <= ((fixed_t)node->x << FRACBITS))
	    return node->dy > 0;

	return node->dy < 0;
    }
    
    if (!node->dy)
    {
	// (id's slip, kept: x against the line's y)
	if (x==((fixed_t)node->y << FRACBITS))
	    return 2;

	if (y <= ((fixed_t)node->y << FRACBITS))
	    return node->dx < 0;

	return node->dx > 0;
    }
	
    dx = (x - ((fixed_t)node->x << FRACBITS));
    dy = (y - ((fixed_t)node->y << FRACBITS));

    left =  muls(node->dy, dx>>FRACBITS);
    right = muls(dy>>FRACBITS, node->dx);
	
    if (right < left)
	return 0;	// front side
    
    if (left == right)
	return 2;
    return 1;		// back side
}


//
// P_SightCrossLine
// The sight line and seg's line have each other's ends on both sides, by
// the first test: the rest of id's P_CrossSubsector loop for it. False if
// the line blocks the sight.
//
static boolean P_SightCrossedLine (const seg_t* seg, divline_t* divl);

static __attribute__((noinline)) boolean P_SightCrossLine (const seg_t* seg)
{
    line_t*		line;
    int			s1;
    int			s2;
    divline_t		divl;
    vertex_t*		v1;
    vertex_t*		v2;

    line = seg->linedef;
    v1 = line->v1;
    v2 = line->v2;

    divl.x = v1->x;
    divl.y = v1->y;
    divl.dx = v2->x - v1->x;
    divl.dy = v2->y - v1->y;
    s1 = P_DivlineSide (strace.x, strace.y, &divl);
    s2 = P_DivlineSide (t2x, t2y, &divl);

    // line isn't crossed?
    if (s1 == s2)
	return true;

    return P_SightCrossedLine (seg, &divl);
}


//
// P_SightCrossedLine
// The sight line crosses seg's line, divl: the rest of id's loop for it.
//
static boolean P_SightCrossedLine (const seg_t* seg, divline_t* divl)
{
    line_t*		line = seg->linedef;
    sector_t*		front;
    sector_t*		back;
    fixed_t		opentop;
    fixed_t		openbottom;
    fixed_t		frac;
    fixed_t		slope;

    // stop because it is not two sided anyway
    // might do this after updating validcount?
    if ( !(line->flags & ML_TWOSIDED) )
	return false;
    
    // crosses a two sided line
    front = seg->frontsector;
    back = seg->backsector;

    // no wall to block sight with?
    if (front->floorheight == back->floorheight
	&& front->ceilingheight == back->ceilingheight)
	return true;

    // possible occluder
    // because of ceiling height differences
    if (front->ceilingheight < back->ceilingheight)
	opentop = front->ceilingheight;
    else
	opentop = back->ceilingheight;

    // because of ceiling height differences
    if (front->floorheight > back->floorheight)
	openbottom = front->floorheight;
    else
	openbottom = back->floorheight;
		
    // quick test for totally closed doors
    if (openbottom >= opentop)	
	return false;		// stop
    
    frac = P_InterceptVector2 (&strace, divl);
		
    if (front->floorheight != back->floorheight)
    {
	slope = FixedDiv (openbottom - sightzstart , frac);
	if (slope > bottomslope)
	    bottomslope = slope;
    }
		
    if (front->ceilingheight != back->ceilingheight)
    {
	slope = FixedDiv (opentop - sightzstart , frac);
	if (slope < topslope)
	    topslope = slope;
    }
		
    if (topslope <= bottomslope)
	return false;		// stop				

    return true;
}


//
// P_CrossSubsector
// Returns true
//  if strace crosses the given subsector successfully.
//
// Most lines a sight check meets it has already checked, or have both
// ends on one side of the sight line: that much is the loop below, on
// sightsegs[] and linevalid[] (10 and 4 bytes a seg, where the seg, its
// line and two vertices were five lines of a 68030's data cache), small
// enough to stay in the instruction cache. The same lines in the same
// order, and P_DivlineSide's arithmetic for each end:
//   left = (strace.dy>>16) * ((v.x - strace.x)>>16), v.x a whole number of
//   map units << 16, so (v.x - strace.x)>>16 is v.x>>16 + (-strace.x)>>16
//   in 16 bits; right likewise. Side 0 is right < left, 2 equal, 1 over.
// A sight line exactly along x or y takes P_DivlineSide's other branches,
// so those (rare) go through it as they always did.
//
__attribute__((noinline)) boolean P_CrossSubsector (int num)
{
    const sightseg_t*	ss;
    const subsector_t*	sub;
    int			count;
    int			i;
    int*		valid;
	
#ifdef RANGECHECK
    if (num>=numsubsectors)
	I_Error ("P_CrossSubsector: ss %i with numss = %i",
		 num,
		 numsubsectors);
#endif

    sub = &subsectors[num];
    
    // check lines
    count = sub->numlines;
    ss = &sightsegs[sub->firstline];

    if (!strace.dx || !strace.dy)
    {
	for (i=0 ; i<count ; i++, ss++)
	{
	    const line_t*	line = &lines[ss->line];
	    int			s1;
	    int			s2;

	    // allready checked other side?
	    valid = &linevalid[ss->line];
	    if (*valid == validcount)
		continue;
	    
	    *valid = validcount;
		    
	    s1 = P_DivlineSide (line->v1->x, line->v1->y, &strace);
	    s2 = P_DivlineSide (line->v2->x, line->v2->y, &strace);

	    // line isn't crossed?
	    if (s1 == s2)
		continue;

	    if (!P_SightCrossLine (&segs[sub->firstline + i]))
		return false;
	}
	// passed the subsector ok
	return true;
    }

    {
	const int	vc = validcount;
	int* const	lv = linevalid;
	const short	sdx = strace.dx >> FRACBITS;
	const short	sdy = strace.dy >> FRACBITS;
	const short	nsx = (fixed_t)(0u - (unsigned)strace.x) >> FRACBITS;
	const short	nsy = (fixed_t)(0u - (unsigned)strace.y) >> FRACBITS;

	for (i=0 ; i<count ; i++, ss++)
	{
	    int32_t	left;
	    int32_t	right;
	    int		s1;
	    int		s2;

	    // allready checked other side?
	    valid = &lv[ss->line];
	    if (*valid == vc)
		continue;
	    
	    *valid = vc;

	    left = muls (sdy, (short)(ss->x + nsx));
	    right = muls ((short)(ss->y + nsy), sdx);
	    s1 = right < left ? 0 : right == left ? 2 : 1;
	    left = muls (sdy, (short)(ss->v2x + nsx));
	    right = muls ((short)(ss->v2y + nsy), sdx);
	    s2 = right < left ? 0 : right == left ? 2 : 1;

	    // line isn't crossed?
	    if (s1 == s2)
		continue;

	    if (!P_SightCrossLine (&segs[sub->firstline + i]))
		return false;
	}
    }
    // passed the subsector ok
    return true;		
}



//
// P_CrossBSPNode
// Returns true
//  if strace crosses the given node successfully.
//
#ifdef TTDOOM
// The BSP walk as a loop with its own stack of the nodes whose far side is
// still to come, rather than a recursive call per node: the same nodes and
// subsectors, in the same order, as id's recursion (below).
#define SIGHTSTACK	128

boolean P_CrossBSPNode (int bspnum)
{
    const node_t*	stack[SIGHTSTACK];
    char	sides[SIGHTSTACK];
    int		sp = 0;
    const node_t*	bsp;
    int		side;

    for ( ; ; )
    {
	// Down the starting sides to a subsector
	while (!(bspnum & NF_SUBSECTOR))
	{
	    bsp = &nodes[bspnum];
	    // decide which side the start point is on
	    side = P_NodeSide (strace.x, strace.y, bsp);
	    if (side == 2)
		side = 0;	// an "on" should cross both sides
	    if (sp == SIGHTSTACK)
		I_Error ("P_CrossBSPNode: BSP deeper than %i", SIGHTSTACK);
	    stack[sp] = bsp;
	    sides[sp++] = side;
	    // cross the starting side
	    bspnum = bsp->children[side];
	}
	if (!P_CrossSubsector (bspnum == -1 ? 0 : bspnum&(~NF_SUBSECTOR)))
	    return false;

	// Crossed: up to a node whose ending side is still to cross
	for ( ; ; )
	{
	    if (!sp)
		return true;
	    bsp = stack[--sp];
	    side = sides[sp];
	    // the partition plane is crossed here
	    if (side != P_NodeSide (t2x, t2y, bsp))
		break;
	    // the line doesn't touch the other side: this node is crossed
	}
	// cross the ending side
	bspnum = bsp->children[side^1];
    }
}
#else
boolean P_CrossBSPNode (int bspnum)
{
    node_t*	bsp;
    int		side;

    if (bspnum & NF_SUBSECTOR)
    {
	if (bspnum == -1)
	    return P_CrossSubsector (0);
	else
	    return P_CrossSubsector (bspnum&(~NF_SUBSECTOR));
    }
		
    bsp = &nodes[bspnum];
    
    // decide which side the start point is on
    side = P_NodeSide (strace.x, strace.y, bsp);
    if (side == 2)
	side = 0;	// an "on" should cross both sides

    // cross the starting side
    if (!P_CrossBSPNode (bsp->children[side]) )
	return false;
	
    // the partition plane is crossed here
    if (side == P_NodeSide (t2x, t2y, bsp))
    {
	// the line doesn't touch the other side
	return true;
    }
    
    // cross the ending side		
    return P_CrossBSPNode (bsp->children[side^1]);
}
#endif


#if defined(USEASM) && defined(__M68020__)
//
// P_SightTrace
// P_CrossBSPNode and P_CrossSubsector's loop (above) as one routine small
// enough to stay in a 68030's 256-byte instruction cache, for a sight line
// along neither x nor y: the same nodes, subsectors and lines in the same
// order, and the same tests. The C versions walked the tree in one loop
// and the lines in another, each pushing the other out of the cache, and
// took 6 cycles an instruction.
//
// The nodes still to cross on their far side are on the stack (node,
// side), a null node under them. A sight record starts as a node does, so
// one routine gives the side of a node's line and of a seg's. A line the
// sight line crosses goes to P_SightCrossedLineAt, id's code for it.
//
typedef struct
{
    fixed_t		x;		// 0: strace.x, .y
    fixed_t		y;
    fixed_t		t2x;		// 8
    fixed_t		t2y;
    const node_t*	nodes;		// 16
    const sightsub_t*	sightsubs;	// 20
    const sightseg_t*	sightsegs;	// 24
    short		sdx;		// 28: strace.dx, .dy >> FRACBITS
    short		sdy;
    short		nsx;		// 32: -strace.x, .y >> FRACBITS
    short		nsy;
    int			validcount;	// 36
    int*		linevalid;	// 40
} sighttrace_t;

ASM_CDECL boolean P_SightTrace (int bspnum, const sighttrace_t* t);

ASM_CDECL __attribute__((used, noinline)) boolean
P_SightCrossedLineAt (const sightseg_t* ss)
{
    const seg_t*	seg = &segs[ss - sightsegs];
    const line_t*	line = seg->linedef;
    divline_t		divl;

    divl.x = line->v1->x;
    divl.y = line->v1->y;
    divl.dx = line->v2->x - line->v1->x;
    divl.dy = line->v2->y - line->v1->y;
    return P_SightCrossedLine (seg, &divl);
}

__asm__ (
"	.text\n"
"	.even\n"
"	.globl	" ASM_SYM(P_SightTrace) "\n"
ASM_SYM(P_SightTrace) ":\n"
"	movem.l	%d2-%d7/%a2-%a6,-(%sp)\n"
"	move.l	48(%sp),%d0\n"			// bspnum
"	move.l	52(%sp),%a6\n"			// the trace
"	movem.w	28(%a6),%d2-%d5\n"		// sdx, sdy, nsx, nsy
"	move.l	16(%a6),%a4\n"			// nodes
"	move.l	36(%a6),%a5\n"			// validcount
"	move.l	40(%a6),%a2\n"			// linevalid
"	clr.l	-(%sp)\n"			// the node stack's end
"	jbra	.Lsight_down\n"

// From here to _cache1 is up to 288 bytes, and the instruction cache holds
// 256: the first and last 32 bytes or so share lines. So first comes what
// runs least and seldom between the last part's runs.
ASM_SYM(P_SightTrace) "_cache0:\n"
// A line's ends are on both sides of the sight line: are the sight line's
// on both sides of it?
".Lsight_cross:\n"
"	move.l	%a3,%a0\n"
"	lea	(%a6),%a1\n"			// its start
"	jbsr	.Lsight_side\n"
"	move.w	%d0,%d7\n"
"	addq.l	#8,%a1\n"			// its end
"	jbsr	.Lsight_side\n"
"	cmp.w	%d0,%d7\n"
"	jbeq	.Lsight_next\n"
"	jbra	.Lsight_crossed\n"
".Lsight_on:\n"
"	moveq	#2,%d0\n"
"	rts\n"
".Lsight_side2:\n"
"	move.w	(%a1),%d0\n"			// (x - node x) >> 16
"	sub.w	(%a0),%d0\n"
"	muls.w	6(%a0),%d0\n"			// left = dy * that
"	move.w	4(%a1),%d1\n"
"	sub.w	2(%a0),%d1\n"
"	muls.w	4(%a0),%d1\n"			// right = (y - node y) >> 16 * dx
"	cmp.l	%d0,%d1\n"
"	jbeq	.Lsight_on\n"
"	sgt	%d0\n"				// front (0) if right < left
".Lsight_bool:\n"
"	and.w	#1,%d0\n"
"	rts\n"

// P_NodeSide: a1 = the point (x, y), a0 = node or sight record; side 0, 1
// or 2 in d0.w. Uses d1.
".Lsight_side:\n"
"	tst.w	4(%a0)\n"			// dx
"	jbne	.Lsight_side1\n"
"	move.w	(%a0),%d1\n"			// the line is x = node x
"	swap	%d1\n"
"	clr.w	%d1\n"
"	move.l	(%a1),%d0\n"
"	cmp.l	%d1,%d0\n"
"	jbeq	.Lsight_on\n"
"	jbgt	.Lsight_side0\n"
"	tst.w	6(%a0)\n"			// x <= node x: dy > 0
"	sgt	%d0\n"
"	jbra	.Lsight_bool\n"
".Lsight_side0:\n"
"	tst.w	6(%a0)\n"			// dy < 0
"	slt	%d0\n"
"	jbra	.Lsight_bool\n"
".Lsight_side1:\n"
"	tst.w	6(%a0)\n"			// dy
"	jbne	.Lsight_side2\n"
"	move.w	2(%a0),%d1\n"			// the line is y = node y
"	swap	%d1\n"
"	clr.w	%d1\n"
"	cmp.l	(%a1),%d1\n"			// (id's slip: x against it)
"	jbeq	.Lsight_on\n"
"	cmp.l	4(%a1),%d1\n"
"	jblt	.Lsight_side3\n"
"	tst.w	4(%a0)\n"			// y <= node y: dx < 0
"	slt	%d0\n"
"	jbra	.Lsight_bool\n"
".Lsight_side3:\n"
"	tst.w	4(%a0)\n"			// dx > 0
"	sgt	%d0\n"
"	jbra	.Lsight_bool\n"

// Down the starting sides to a subsector: d0.w = node or subsector
".Lsight_node:\n"
"	lea	(%a4,%d0.w*8),%a0\n"		// nodes are 16 bytes
"	lea	(%a0,%d0.w*8),%a0\n"
"	lea	(%a6),%a1\n"
"	jbsr	.Lsight_side\n"
"	and.w	#1,%d0\n"			// on the line (2): both sides, front first
"	move.w	%d0,-(%sp)\n"
"	move.l	%a0,-(%sp)\n"
"	move.w	8(%a0,%d0.w*2),%d0\n"		// cross the starting side
".Lsight_down:\n"
"	tst.w	%d0\n"
"	jbpl	.Lsight_node\n"

// A subsector's lines: a3 = sight record, d6 = the end of them
"	and.w	#0x7fff,%d0\n"
"	move.l	20(%a6),%a0\n"			// sightsubs
"	lea	(%a0,%d0.w*8),%a0\n"
"	move.l	(%a0)+,%a3\n"
"	move.l	(%a0),%d6\n"
"	cmp.l	%d6,%a3\n"
"	jbeq	.Lsight_pop\n"
".Lsight_seg:\n"
"	move.w	12(%a3),%d0\n"			// the line
"	lea	(%a2,%d0.w*4),%a0\n"
"	cmp.l	(%a0),%a5\n"			// already checked?
"	jbeq	.Lsight_next\n"
"	move.l	%a5,(%a0)\n"
"	move.w	(%a3),%d0\n"			// its first end against the sight
"	add.w	%d4,%d0\n"			// line: left = sdy * (v.x - x >> 16)
"	muls.w	%d3,%d0\n"
"	move.w	2(%a3),%d1\n"
"	add.w	%d5,%d1\n"			// right = (v.y - y >> 16) * sdx
"	muls.w	%d2,%d1\n"
"	sub.l	%d0,%d1\n"			// side 0 below 0, 2 at 0, 1 above
"	move.w	8(%a3),%d0\n"			// its second end
"	add.w	%d4,%d0\n"
"	muls.w	%d3,%d0\n"
"	move.w	10(%a3),%d7\n"
"	add.w	%d5,%d7\n"
"	muls.w	%d2,%d7\n"
"	sub.l	%d0,%d7\n"
"	jbeq	.Lsight_on2\n"
"	tst.l	%d1\n"
"	jbeq	.Lsight_cross\n"
"	eor.l	%d1,%d7\n"
"	jbmi	.Lsight_cross\n"
".Lsight_next:\n"
"	lea	16(%a3),%a3\n"
"	cmp.l	%d6,%a3\n"
"	jbne	.Lsight_seg\n"

// The subsector is crossed: up to a node whose ending side is still to cross
".Lsight_pop:\n"
"	move.l	(%sp)+,%d0\n"
"	jbeq	.Lsight_true\n"
"	move.l	%d0,%a0\n"
"	move.w	(%sp)+,%d7\n"			// the side crossed first
"	lea	8(%a6),%a1\n"
"	jbsr	.Lsight_side\n"
"	cmp.w	%d0,%d7\n"			// the sight line ends on it: this
"	jbeq	.Lsight_pop\n"			// node is crossed
"	eor.w	#1,%d7\n"
"	move.w	8(%a0,%d7.w*2),%d0\n"		// cross the ending side
"	jbra	.Lsight_down\n"
ASM_SYM(P_SightTrace) "_cache1:\n"

".Lsight_on2:\n"
"	tst.l	%d1\n"				// both ends on the sight line
"	jbeq	.Lsight_next\n"
"	jbra	.Lsight_cross\n"
".Lsight_crossed:\n"
"	move.l	%a3,-(%sp)\n"
"	jsr	" ASM_SYM(P_SightCrossedLineAt) "\n"
"	addq.l	#4,%sp\n"
"	tst.l	%d0\n"
"	jbne	.Lsight_next\n"
".Lsight_false:\n"				// blocked: drop the node stack
"	move.l	(%sp)+,%d0\n"
"	jbeq	.Lsight_ret\n"
"	addq.l	#2,%sp\n"
"	jbra	.Lsight_false\n"
".Lsight_true:\n"
"	moveq	#1,%d0\n"
".Lsight_ret:\n"
"	movem.l	(%sp)+,%d2-%d7/%a2-%a6\n"
"	rts\n"
);
#endif


//
// P_CheckSight
// Returns true
//  if a straight line between t1 and t2 is unobstructed.
// Uses REJECT.
//
boolean
P_CheckSight
( mobj_t*	t1,
  mobj_t*	t2 )
{
    short	s1;
    short       s2;
    int		pnum;
    int		bytenum;
    int		bitnum;
    
    // First check for trivial rejection.

    // Determine subsector entries in REJECT table.
    s1 = (t1->subsector->sector - sectors);
    s2 = (t2->subsector->sector - sectors);
    pnum = muls(s1, numsectors) + s2;
    bytenum = pnum>>3;
    bitnum = 1 << (pnum&7);

    // Check in REJECT table.
    if (rejectmatrix[bytenum]&bitnum)
    {
	sightcounts[0]++;

	// can't possibly be connected
	return false;	
    }

    // An unobstructed LOS is possible.
    // Now look from eyes of t1 to any part of t2.
    sightcounts[1]++;

    validcount++;
	
    sightzstart = t1->z + t1->height - (t1->height>>2);
    topslope = (t2->z+t2->height) - sightzstart;
    bottomslope = (t2->z) - sightzstart;
	
    strace.x = t1->x;
    strace.y = t1->y;
    t2x = t2->x;
    t2y = t2->y;
    strace.dx = t2->x - t1->x;
    strace.dy = t2->y - t1->y;

#if defined(USEASM) && defined(__M68020__)
    if (strace.dx && strace.dy && numnodes)
    {
	sighttrace_t	t;

	t.x = strace.x;
	t.y = strace.y;
	t.t2x = t2x;
	t.t2y = t2y;
	t.nodes = nodes;
	t.sightsubs = sightsubs;
	t.sightsegs = sightsegs;
	t.sdx = strace.dx >> FRACBITS;
	t.sdy = strace.dy >> FRACBITS;
	t.nsx = (fixed_t)(0u - (unsigned)strace.x) >> FRACBITS;
	t.nsy = (fixed_t)(0u - (unsigned)strace.y) >> FRACBITS;
	t.validcount = validcount;
	t.linevalid = linevalid;
	return P_SightTrace (numnodes-1, &t);
    }
#endif

    // the head node is the last node output
    return P_CrossBSPNode (numnodes-1);	
}


