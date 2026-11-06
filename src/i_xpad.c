//
// Gamepads through Xpad.
//
// Xpad (github.com/neilrackett/atarist-xpad) is a block of pad state that
// a provider publishes through the cookie jar: Bluetooth pads through a
// SidecarTridge (MD/Sidepad) or the serial port (COMpad), Jaguar pads in
// the STE and Falcon enhanced ports (XPADSTE), and so on. STDOOM reads
// the IKBD itself (i_video.c replaces the ACIA vector), so a provider's
// joystick and mouse injection never reaches it: it reads the block.
//
// The pad drives Doom through its own key bindings, the reverse of
// Xpad's keyboard driver (DOOM's default controls): the d-pad is the
// arrow keys, south fires, east uses, west strafes, north runs, the
// shoulders strafe left and right, select is the automap and start the
// menu. The right trigger fires too and the left trigger runs. In the
// menus the d-pad moves, south selects (yes to a question) and east goes
// back (no). With analogue sticks the left stick moves and strafes and
// the right one turns, in proportion (I_XpadMove).
//
// Joystick 1 counts too, as a one-button pad (fire is south), with or
// without a provider.
//

#include <stdio.h>
#include <string.h>

#include "doomdef.h"
#include "doomstat.h"
#include "d_event.h"
#include "d_main.h"
#include "m_argv.h"
#include "i_xpad.h"
#include "xpad.h"

extern int	key_right, key_left, key_up, key_down;
extern int	key_strafeleft, key_straferight;
extern int	key_fire, key_use, key_strafe, key_speed;
extern int	messageToPrint;
extern boolean	messageNeedsInput;

// Below this, a stick is centred. Past it the stick ramps from 0.
#define STICK_DEADZONE	24
// A left stick this far off centre is what set the d-pad bits (providers
// fold the stick into them), so in play they don't turn as well.
#define STICK_FOLDED	32

// IKBD joystick packet: up, down, left and right are bits 0-3, the same
// as XPAD_UP..XPAD_RIGHT, and fire is bit 7
#define JOY_FIRE	0x80

static const XPAD	*xpad;
static XPAD_PAD		pad;		// this tic's pad
static unsigned char	joy1;		// joystick 1, from the IKBD
static uint32_t		held;		// buttons down last tic
static int		held_key[32];	// the key each held button pressed


void I_InitXpad (void)
{
    if (M_CheckParm ("-noxpad"))
	return;

    // Uses Supexec: called from user mode, before I_Init enters supervisor
    xpad = xpad_find ();
    if (xpad)
	printf ("Xpad: %s, %d pads\n",
		xpad->provider ? xpad->provider : "provider", xpad->pad_count);
}


void I_XpadJoystick (unsigned char data)
{
    joy1 = data;
}


//
// The first pad plugged in. A read that tore on every retry keeps last
// tic's state rather than letting go of everything for a tic.
//
static void read_pad (void)
{
    XPAD_PAD	p;
    int		i;

    if (!xpad)
	return;
    for (i = 0 ; i < xpad->pad_count ; i++)
    {
	if (!xpad_read (xpad, i, &p))
	    return;
	if (p.type != XPAD_TYPE_NONE)
	{
	    pad = p;
	    return;
	}
    }
    memset (&pad, 0, sizeof(pad));
}


static boolean stick_off_centre (int8_t x, int8_t y)
{
    return x >= STICK_FOLDED || x <= -STICK_FOLDED
	|| y >= STICK_FOLDED || y <= -STICK_FOLDED;
}


static uint32_t buttons_now (boolean menu)
{
    uint32_t	b = pad.buttons;

    // In play an analogue left stick moves and strafes (I_XpadMove), so
    // the d-pad bits folded from it must not walk and turn as well
    if (!menu && (pad.flags & XPAD_PAD_ANALOG)
	&& stick_off_centre (pad.lx, pad.ly))
	b &= ~XPAD_DPAD;

    b |= joy1 & XPAD_DPAD;
    if (joy1 & JOY_FIRE)
	b |= XPAD_SOUTH;
    return b;
}


//
// The key a button presses, or 0: in play, through Doom's key bindings;
// in the menus, the keys the menus take.
//
static int button_key (uint32_t button, boolean menu)
{
    boolean	question = messageToPrint && messageNeedsInput;

    if (menu)
    {
	switch (button)
	{
	  case XPAD_UP:		return KEY_UPARROW;
	  case XPAD_DOWN:	return KEY_DOWNARROW;
	  case XPAD_LEFT:	return KEY_LEFTARROW;
	  case XPAD_RIGHT:	return KEY_RIGHTARROW;
	  case XPAD_SOUTH:
	  case XPAD_TR2:	return question ? 'y' : KEY_ENTER;
	  case XPAD_EAST:	return question ? 'n' : KEY_BACKSPACE;
	  case XPAD_START:	return KEY_ESCAPE;
	  default:		return 0;
	}
    }
    switch (button)
    {
      case XPAD_UP:		return key_up;
      case XPAD_DOWN:		return key_down;
      case XPAD_LEFT:		return key_left;
      case XPAD_RIGHT:		return key_right;
      case XPAD_SOUTH:
      case XPAD_TR2:		return key_fire;
      case XPAD_EAST:		return key_use;
      case XPAD_WEST:		return key_strafe;
      case XPAD_NORTH:
      case XPAD_TL2:		return key_speed;
      case XPAD_TL:		return key_strafeleft;
      case XPAD_TR:		return key_straferight;
      case XPAD_SELECT:		return KEY_TAB;
      case XPAD_START:		return KEY_ESCAPE;
      default:			return 0;
    }
}


// Whether a button in mask, other than number i, is holding key down
static boolean key_held (int key, uint32_t mask, int i)
{
    int		j;

    for (j = 0 ; j < 32 ; j++)
	if (j != i && (mask & (1UL << j)) && held_key[j] == key)
	    return true;
    return false;
}


static void post_key (evtype_t type, int key)
{
    event_t	ev;

    ev.type = type;
    ev.data1 = key;
    ev.data2 = ev.data3 = 0;
    D_PostEvent (&ev);
}


//
// Posts presses and releases. A button releases the key it pressed, even
// if the menu opened or closed while it was down, and nothing is pressed
// by a button already down when the menu changed: holding fire while
// opening the menu does not select its first item. Two buttons on one key
// (south and the right trigger both fire) press it once.
//
void I_XpadStartTic (void)
{
    boolean	menu = menuactive;
    uint32_t	now, changed, bit;
    int		i;

    read_pad ();
    now = buttons_now (menu);
    changed = now ^ held;

    for (i = 0, bit = 1 ; changed ; i++, bit <<= 1)
    {
	int	key;

	if (!(changed & bit))
	    continue;
	changed &= ~bit;
	if (now & bit)
	{
	    key = button_key (bit, menu);
	    held_key[i] = key;
	    if (key && !key_held (key, now, i))
		post_key (ev_keydown, key);
	}
	else
	{
	    key = held_key[i];
	    held_key[i] = 0;
	    if (key && !key_held (key, now, i))
		post_key (ev_keyup, key);
	}
    }
    held = now;
}


//
// A stick axis past the dead zone, ramping 0..127 from its edge.
// 127/103 is about 79/64. 16-bit arithmetic throughout: a 68000 has no
// 32-bit multiply, so wider types would make each one a libcall.
//
static short stick (int8_t v)
{
    short	a = v < 0 ? -v : v;

    if (a < STICK_DEADZONE)
	return 0;
    a = (short)((a - STICK_DEADZONE) * 79) >> 6;
    if (a > 127)
	a = 127;
    return v < 0 ? -a : a;
}


void I_XpadMove (int *forward, int *side, short *turn,
		 short fullforward, short fullside, short fullturn)
{
    short	lx, ly, rx, q;

    if (menuactive || !(pad.flags & XPAD_PAD_ANALOG))
	return;

    lx = stick (pad.lx);
    ly = stick (pad.ly);
    rx = stick (pad.rx);

    // +y is down: pushing the stick away walks forward. /128 by shifts.
    *forward -= (short)(ly * fullforward) >> 7;
    *side += (short)(lx * fullside) >> 7;

    // Squared, for fine aim near the centre and full speed at the edge:
    // full deflection turns about as fast as the arrow keys when running
    q = (short)(rx * (rx < 0 ? -rx : rx)) >> 7;
    *turn -= (short)(q * (short)(fullturn >> 4)) >> 3;
}
