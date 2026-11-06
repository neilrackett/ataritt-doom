#ifndef ATARI_VMODE_H
#define ATARI_VMODE_H

/*
 * TTDOOM display: 320x240 in 256 colours, eight interleaved bitplanes.
 *
 *   TT      TT Low (320x480). TT Low cannot double lines itself, so the C2P
 *           writes every picture line to two scanlines.
 *   Falcon  VIDEL 8-bit. VGA: 320x240 with the VIDEL doubling lines.
 *           RGB / TV: 240 lines need overscan, so the screen is 384 pixels
 *           wide with the 320-pixel picture 32 pixels in.
 *
 * The approach follows atarist-stdl's responsive screens (src/responsive.c).
 */

typedef struct {
    unsigned char *screen;  /* first byte of the 320x240 picture */
    unsigned short stride;  /* bytes from one picture line to the next */
    unsigned char falcon;   /* palette registers: Falcon $FF9800, else TT $FF8400 */
} atari_vmode_t;

/* User mode, first thing in main(): 1 on a TT or a Falcon. Otherwise prints
 * why TTDOOM cannot run, waits for a key and returns 0. */
int atari_vmode_supported(void);

/* User mode, before Doom's zone is allocated: sets aside the ST-RAM for the
 * display pages, so that on a 4 MB machine the zone cannot take it. */
void atari_vmode_reserve(void);

/* Supervisor mode. Saves the desktop's mode and colours, sets the 320x240
 * 256-colour mode on fresh, cleared ST-RAM screens and fills *m (m->screen
 * is the page on screen). I_Error if
 * the monitor or memory cannot do it. */
void atari_vmode_set(atari_vmode_t *m);

/* Supervisor mode. Double buffering: the page to draw the next frame into,
 * at the same picture offset and stride as m->screen. Waits, if need be,
 * until the page shown before the last atari_vmode_show_page() has left the
 * screen. With one page (short of ST-RAM) this is the page on screen. */
unsigned char *atari_vmode_draw_page(void);

/* Supervisor mode. Shows the page from atari_vmode_draw_page() from the
 * next frame on. */
void atari_vmode_show_page(void);

/* Supervisor mode. Puts the desktop's mode and colours back; does nothing if
 * atari_vmode_set() never ran. Safe to call from I_Error. */
void atari_vmode_restore(void);

#endif /* ATARI_VMODE_H */
