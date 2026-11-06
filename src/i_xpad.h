#ifndef __I_XPAD__
#define __I_XPAD__

#include "doomtype.h"

//
// Gamepads through Xpad (github.com/neilrackett/atarist-xpad), and
// joystick 1 as a one-button pad alongside. See i_xpad.c.
//

// User mode, at startup: looks for an Xpad provider (-noxpad skips it).
void I_InitXpad (void);

// The data byte of an IKBD joystick 1 packet, from I_StartTic.
void I_XpadJoystick (unsigned char data);

// Once a tic, after the keyboard: posts the pad's changes as key events.
void I_XpadStartTic (void);

// G_BuildTiccmd: adds analogue stick movement (left stick: forward and
// strafe, right stick: turn) scaled to the given full-deflection moves.
void I_XpadMove (int *forward, int *side, short *turn,
		 short fullforward, short fullside, short fullturn);

#endif
