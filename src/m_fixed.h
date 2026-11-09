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

// The 68000's cheaper multiplies are slower than FixedMul here, an inline
// muls.l. FixedScale32 (b in (-1, 1)) gives exactly FixedMul's result for
// |b| < 65536, and every caller passes finesine/finecosine (at most 65535)
// or FRICTION (checked on the host: 200 million products and every sine
// value against every edge). FixedMulShort's callers pass post offsets
// (0-255): a plain product. FixedScale (b a Q0.15 short) is FixedMul by 2b;
// the 68000 version drops a's fraction (an operator precedence slip), so
// sprites, its only user, move by up to a unit.
#define FixedScale32(a, b)	FixedMul ((a), (b))
#define FixedMulShort(a, b)	((fixed_t)(a) * (unsigned short)(b))
#define FixedScale(a, b)	FixedMul ((a), (fixed_t)(b) << 1)

// Division: one 64/32 divs.l of a<<16 by b, which truncates toward zero as
// id's (DOS) FixedDiv does. Behind the same guard as the 68000's, which
// keeps the quotient below 2^30. The 68000's bit-by-bit division gives the
// same for |a| < 2^29 and |b| < 2^30 (checked on the host for 181 million
// pairs); above that its shifts overflow, and divs.l is the one that agrees
// with DOS. The 68060 traps the 64-bit form and keeps FixedDivSoft.
fixed_t FixedDivSoft	(fixed_t a, fixed_t b);

static inline fixed_t FixedDiv (fixed_t a, fixed_t b)
{
    if (fixedmul_muls64 && a != (int)0x80000000)
    {
	// The guard as the 68000's computes it, abs(MININT) negative
	unsigned	ua = a < 0 ? 0u - (unsigned)a : (unsigned)a;
	unsigned	ub = b < 0 ? 0u - (unsigned)b : (unsigned)b;
	fixed_t		hi;

	if ((int)(ua >> 14) >= (int)ub)
	    return (a ^ b) < 0 ? (int)0x80000000 : 0x7fffffff;
	hi = a >> 16;
	a = (fixed_t)((unsigned)a << 16);
	// hi:a / b, the quotient in a
	__asm__ ("divs.l	%2,%1:%0"
		 : "+d" (a), "+d" (hi)
		 : "d" (b)
		 : "cc");
	return a;
    }
    return FixedDivSoft (a, b);
}
#else
fixed_t FixedMul	(fixed_t a, fixed_t b);

// Multiplication of a Q15.16 fixed point value with a Q0.15 bit value representing range [-1; 1)
fixed_t FixedScale	(fixed_t a, short b);

// Multiplication of two Q15.16 fixed point values a and b where b is known to be in range (-1; 1).
fixed_t FixedScale32	(fixed_t a, fixed_t b);

// Multiplication of a Q15.16 fixed point value with a 16 bit signed integer
fixed_t FixedMulShort	(fixed_t a, short b);

// Division of two Q15.16 fixed point values.
fixed_t FixedDiv	(fixed_t a, fixed_t b);
#endif

// At startup: picks the fastest FixedMul this CPU can run.
void M_InitFixed (void);



#endif
//-----------------------------------------------------------------------------
//
// $Log:$
//
//-----------------------------------------------------------------------------
