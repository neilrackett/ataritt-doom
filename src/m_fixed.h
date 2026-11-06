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
//	Fixed point arithemtics, implementation.
//
//-----------------------------------------------------------------------------


#ifndef __M_FIXED__
#define __M_FIXED__


#ifdef __GNUG__
#pragma interface
#endif


//
// Fixed point, 32bit as 16.16.
//
#define FRACBITS		16
#define FRACUNIT		(1<<FRACBITS)

typedef int fixed_t;

// Multiplication of two Q15.16 fixed point values
// 68020 to 68040: FixedMul is one 64-bit muls.l, inline, as it is called a
// thousand times a frame. It gives exactly what the 68000's split multiply
// does (checked for 50 million products on the host, edge cases included).
// The 68060 dropped the 64-bit form (it traps to software), so it calls the
// split multiply, FixedMulSplit. M_InitFixed picks, from the _CPU cookie.
#if defined(__M68020__) && !defined(__HAVE_68881__)
#define FIXEDMUL_MULS64
extern int	fixedmul_muls64;
fixed_t FixedMulSplit	(fixed_t a, fixed_t b);

static inline fixed_t FixedMul (fixed_t a, fixed_t b)
{
    if (fixedmul_muls64)
    {
	fixed_t	hi;

	// The 64-bit product in hi:a; bits 47..16 are the result
	__asm__ ("muls.l	%2,%1:%0\n\t"
		 "move.w	%1,%0\n\t"
		 "swap	%0"
		 : "=d" (a), "=&d" (hi)
		 : "d" (b), "0" (a)
		 : "cc");
	return a;
    }
    return FixedMulSplit (a, b);
}
#else
fixed_t FixedMul	(fixed_t a, fixed_t b);
#endif

// At startup: picks the fastest FixedMul this CPU can run.
void M_InitFixed (void);

// Multiplication of a Q15.16 fixed point value with a Q0.15 bit value representing range [-1; 1)
fixed_t FixedScale	(fixed_t a, short b);

// Multiplication of two Q15.16 fixed point values a and b where b is known to be in range (-1; 1).
fixed_t FixedScale32	(fixed_t a, fixed_t b);

// Multiplication of a Q15.16 fixed point value with a 16 bit signed integer
fixed_t FixedMulShort	(fixed_t a, short b);

// Division of two Q15.16 fixed point values.
fixed_t FixedDiv	(fixed_t a, fixed_t b);



#endif
//-----------------------------------------------------------------------------
//
// $Log:$
//
//-----------------------------------------------------------------------------
