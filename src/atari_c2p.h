#ifndef ATARI_C2P_H
#define ATARI_C2P_H

void init_c2p_table();

// 8-plane 256-colour screen (TT Low, Falcon 8-bit): stride = bytes from one
// picture line to the next; dbl = also write each line to the scanline below
// (TT Low); falcon = Falcon palette registers instead of the TT's.
void init_c2p_8bpp(unsigned short stride, int dbl, int falcon);
// TTDOOM: the dirty box (BOXTOP..BOXRIGHT) of what changed since the page
// about to be drawn last took a frame. Given before each frame's C2P, it lets
// a shrunk view (sizes 6-9) skip converting the unchanged border.
void c2p_8bpp_set_dirty(const int *box);

void c2p_screen(unsigned char *out, const unsigned char *in);
void c2p_statusbar(unsigned char *out, const unsigned char *in, short y_begin, short y_end, short x_begin, short x_end);
extern void (*set_doom_palette)(const unsigned char *colors);
void draw_palette_table(unsigned char *st_screen);
extern void (*install_palette)(const unsigned short *palette);
extern void (*save_palette)(unsigned short *palette);

/* Software drawfuncs, palette helper, function pointers, and reduction-map
 * source data — exposed so sidecart_c2p.c can install over them and fall back
 * to them when the SidecarTridge accelerator is active. */
extern void (*c2p_screen_drawfunc)(unsigned char *out, const unsigned char *in);
extern void (*c2p_statusbar_drawfunc)(unsigned char *out, const unsigned char *in, short y_begin, short y_end, short x_begin, short x_end);
void c2p_screen_lorez(unsigned char *out, const unsigned char *in);
void c2p_statusbar_lorez(unsigned char *out, const unsigned char *in, short y_begin, short y_end, short x_begin, short x_end);
void set_st_doom_palette(const unsigned char *colors);
extern unsigned char mix_weights_lorez[256][16];

// Optional: override low-res (16-pen) dithering weights from a .W16 file.
// 4116-byte C2P_WeightSet: magic "W16\0", subset[16], weights[256][16].
// subset[pen] = PLAYPAL index for hardware pen pen; weights[src][pen] dither mix weights.
// Returns 1 if installed, 0 on failure.
int atari_try_install_w16(const char *path);
// Try stdoom.w16 in the current directory; silent if missing. Returns 1 if installed.
int atari_try_install_default_w16(void);

#endif // ATARI_C2P_H
