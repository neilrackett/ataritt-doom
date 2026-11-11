#!/usr/bin/env python3
# benchpatch.py TREE: patches a copy of the tree for benchmarking (never the
# repo itself; mkbuild.sh makes the copy).
#
# - main() takes its arguments from ARGS.TXT when started without any:
#   Hatari can't pass arguments to an autostarted program.
# - A demo ends at gametic 600, and prints "BENCH READY" (in main),
#   "BENCH START" (the first demo tic) and, at the end, "BENCH STATE": a hash
#   of every mobj's position, motion, angle, health and state and the random
#   index. Builds whose game logic is the same print the same hash.
# - -benchshot N (repeatable): screens[0] at gametic N to SN.RAW (320x240
#   palette indices) and the TT Low page it was converted to, to PN.RAW
#   (shots.py and pagecheck.py compare them).
# - -mixtest: the 68020 asm mixer against a copy of the C mixer, on random
#   channel states (MIXTEST, the number of mismatches).
# - -c2ptest: c2p8_pairbox1/2 against c2p8_box1/2 on random paired rows
#   (C2PTEST, the number of mismatches).
import sys, re, pathlib
root = pathlib.Path(sys.argv[1])
p = root / "src/i_main.c"
s = p.read_text()
s = s.replace("#include \"m_argv.h\"", "#include \"m_argv.h\"\n#include <stdio.h>\n#include <string.h>\n#include <stdlib.h>", 1)
s = s.replace("""    myargc = argc; 
    myargv = argv; """, """    static char bench_buf[256];
    static char *bench_argv[32];
    if (argc <= 1)
    {
	FILE *f = fopen ("ARGS.TXT", "r");
	if (f)
	{
	    int n = 0;
	    bench_argv[n++] = argv[0];
	    if (fgets (bench_buf, sizeof bench_buf, f))
	    {
		char *t = strtok (bench_buf, " \\t\\r\\n");
		while (t && n < 31)
		{
		    bench_argv[n++] = t;
		    t = strtok (NULL, " \\t\\r\\n");
		}
	    }
	    fclose (f);
	    bench_argv[n] = NULL;
	    argc = n;
	    argv = bench_argv;
	}
    }
    fprintf (stderr, "BENCH READY\\n");
    fflush (stderr);
    myargc = argc; 
    myargv = argv; """, 1)
assert "bench_argv" in s
p.write_text(s)
p = root / "src/g_game.c"
s = p.read_text()
old = "    if (*demo_p == DEMOMARKER) "
assert old in s
s = s.replace(old, """    if (gametic == 1)
    {
	fprintf (stderr, "BENCH START\\n");
	fflush (stderr);
    }
    if (*demo_p == DEMOMARKER || gametic >= 600)
    {
	// Game state at the demo's end: every mobj's position, motion,
	// angle, health and state, and the random index. Builds whose game
	// logic is the same print the same.
	extern int prndindex;
	thinker_t *th;
	unsigned long h = 2166136261u;
	int n = 0;
	for (th = thinkercap.next; th != &thinkercap; th = th->next)
	{
	    mobj_t *mo;
	    if (th->function.acp1 != (actionf_p1)P_MobjThinker)
		continue;
	    mo = (mobj_t *)th;
	    h = (h ^ (unsigned long)mo->x) * 16777619u;
	    h = (h ^ (unsigned long)mo->y) * 16777619u;
	    h = (h ^ (unsigned long)mo->z) * 16777619u;
	    h = (h ^ (unsigned long)mo->momx) * 16777619u;
	    h = (h ^ (unsigned long)mo->momy) * 16777619u;
	    h = (h ^ (unsigned long)mo->momz) * 16777619u;
	    h = (h ^ (unsigned long)mo->angle) * 16777619u;
	    h = (h ^ (unsigned long)mo->health) * 16777619u;
	    h = (h ^ (unsigned long)(mo->state - states)) * 16777619u;
	    n++;
	}
	fprintf (stderr, "BENCH STATE tic %d mobjs %d rnd %d hash %08lx\\n",
		 gametic, n, prndindex, h);
	fflush (stderr);
    }
    if (*demo_p == DEMOMARKER || gametic >= 600) """, 1)
p.write_text(s)

p = root / "src/i_video.c"
s = p.read_text()
old = "        st_screen = atari_vmode_draw_page();"
assert old in s
s = s.replace(old, """        {
            // -benchshot N (repeatable): screens[0] at gametic N to SN.RAW
            extern int gametic, myargc;
            extern char **myargv;
            static int shots[16], nshots = -1;
            int k;
            if (nshots < 0)
            {
                nshots = 0;
                for (k = 1; k < myargc - 1; k++)
                    if (!strcmp (myargv[k], "-benchshot") && nshots < 16)
                        shots[nshots++] = atoi (myargv[k + 1]);
            }
            for (k = 0; k < nshots; k++)
                if (shots[k] == gametic)
                {
                    char name[16];
                    FILE *f;
                    sprintf (name, "S%d.RAW", gametic);
                    f = fopen (name, "wb");
                    if (f)
                    {
                        fwrite (screens[0], 1, SCREENWIDTH * SCREENHEIGHT, f);
                        fclose (f);
                    }
                    shots[k] = -2 - gametic;
                }
        }
""" + old, 1)
old = "        atari_vmode_show_page();"
assert old in s
s = s.replace(old, """        {
            // the page as converted, for the -benchshot tics: PN.RAW
            extern int gametic;
            extern char **myargv;
            extern int myargc;
            int k;
            for (k = 1; k < myargc - 1; k++)
                if (!strcmp (myargv[k], "-benchshot") && atoi (myargv[k + 1]) == gametic)
                {
                    char name[16];
                    FILE *f;
                    sprintf (name, "P%d.RAW", gametic);
                    f = fopen (name, "wb");
                    if (f)
                    {
                        fwrite (st_screen, 1, 640L * SCREENHEIGHT, f);
                        fclose (f);
                    }
                }
        }
""" + old, 1)
s = "#include <stdio.h>\n#include <stdlib.h>\n#include <string.h>\n" + s
p.write_text(s)

# -mixtest: the 68020 asm mixer against a copy of the C mixer, on random
# channel states; prints MIXTEST and the number of mismatches
p = root / "src/i_sound.c"
s = p.read_text()
# (only in trees that have the asm mixer)
if "mix_first_channel" in s:
  s += """
#ifdef __M68020__
static void old_mix_channel(short *out, short chan, boolean first)
{
  char *leftvol_lookup = channelleftvol_lookup[chan];
  char *rightvol_lookup = channelrightvol_lookup[chan];
  unsigned char *in = channels[chan], *channelend = channelsend[chan];
  unsigned int step = channelstep[chan];
  unsigned int remainder = channelstepremainder[chan];
  short *end = out + SAMPLECOUNT;
  if (in + ((SAMPLECOUNT * step + remainder) >> 16) < channelend)
  {
    while (out != end)
    {
      unsigned char sample = *in;
      short dl = leftvol_lookup[sample] << 8 | (rightvol_lookup[sample] & 0xff);
      if (first) *out++ = dl; else *out++ += dl;
      remainder += step; in += remainder >> 16; remainder &= 0xffff;
    }
    channels[chan] = in;
    channelstepremainder[chan] = remainder;
    return;
  }
  do
  {
    unsigned char sample = *in;
    short dl = leftvol_lookup[sample] << 8 | (rightvol_lookup[sample] & 0xff);
    if (first) *out++ = dl; else *out++ += dl;
    remainder += step; in += remainder >> 16; remainder &= 65536 - 1;
  } while (in < channelend);
  channels[chan] = 0;
  if (first) { while (out != end) *out++ = 0; }
}

void bench_mixtest(void)
{
  static unsigned char snd[3000];
  static char lv[256], rv[256];
  static short a[SAMPLECOUNT], b[SAMPLECOUNT];
  unsigned long r = 12345;
  int i, t, bad = 0;
#define RND (r = r * 1103515245 + 12345, (r >> 8) & 0xffffff)
  for (i = 0; i < 3000; i++) snd[i] = RND;
  for (i = 0; i < 256; i++) { lv[i] = RND; rv[i] = RND; }
  channelleftvol_lookup[0] = lv;
  channelrightvol_lookup[0] = rv;
  for (t = 0; t < 4000; t++)
  {
    int first = t & 1;
    unsigned char *st = snd + RND % 400;
    unsigned char *en = st + 1 + RND % 2000;
    unsigned int step = 1 + RND % (3 * 65536);
    unsigned int rem = RND & 0xffff;
    unsigned char *o_ch; unsigned int o_rem;
    if (t & 2) step = 60000 + RND % 10000;
    for (i = 0; i < SAMPLECOUNT; i++) a[i] = b[i] = RND;
    channels[0] = st; channelsend[0] = en; channelstep[0] = step; channelstepremainder[0] = rem;
    old_mix_channel(a, 0, first);
    o_ch = channels[0]; o_rem = channelstepremainder[0];
    channels[0] = st; channelsend[0] = en; channelstep[0] = step; channelstepremainder[0] = rem;
    if (first) mix_first_channel(b, 0); else mix_channel(b, 0);
    if (memcmp(a, b, sizeof a) || channels[0] != o_ch || (o_ch && channelstepremainder[0] != o_rem))
    {
      if (bad++ < 5)
        fprintf(stderr, "MIXTEST bad t %d first %d step %u rem %u len %ld\\n", t, first, step, rem, (long)(en - st));
    }
  }
  channels[0] = 0;
  fprintf(stderr, "MIXTEST done, %d mismatches\\n", bad);
}
#endif
"""
p.write_text(s)
p = root / "src/i_main.c"
s = p.read_text()
s = s.replace("    D_DoomMain (); ", """    {
	extern void bench_mixtest(void);
	int k;
	for (k = 1; k < myargc; k++)
	    if (!strcmp (myargv[k], "-mixtest"))
		bench_mixtest ();
    }
    D_DoomMain (); """, 1)
assert "bench_mixtest ()" in s
p.write_text(s)

# -c2ptest: c2p8_pairbox1/2 against c2p8_box1/2 on random paired rows
p = root / "src/atari_c2p.c"
s = p.read_text()
# (only in trees that have the pair C2P)
if "c2p8_pairbox" in s:
  s += """
#ifdef TTDOOM
void bench_c2ptest(void)
{
    static unsigned char src[6 * SCREENWIDTH];
    static unsigned char a[6 * 2 * SCREENWIDTH + 64], b[6 * 2 * SCREENWIDTH + 64];
    static const short widths[] = { 1, 2, 3, 7, 20 };
    unsigned long r = 777;
    int i, t, w, rows, bad = 0, runs = 0;
    for (t = 0; t < 40; t++)
        for (w = 0; w < 5; w++)
            for (rows = 1; rows <= 6; rows++)
            {
                for (i = 0; i < 6 * SCREENWIDTH; i += 2)
                {
                    r = r * 1103515245 + 12345;
                    src[i] = src[i + 1] = r >> 16;
                }
                memset(a, 0x5a, sizeof a);
                memset(b, 0x5a, sizeof b);
                c2p8_box2(src, SCREENWIDTH, a, SCREENWIDTH, widths[w], rows);
                c2p8_pairbox2(src, SCREENWIDTH, b, SCREENWIDTH, widths[w], rows);
                runs++;
                if (memcmp(a, b, sizeof a) && bad++ < 5)
                    fprintf(stderr, "C2PTEST box2 bad t %d groups %d rows %d\\n", t, widths[w], rows);
                memset(a, 0x5a, sizeof a);
                memset(b, 0x5a, sizeof b);
                c2p8_box1(src, SCREENWIDTH, a, SCREENWIDTH, widths[w], rows);
                c2p8_pairbox1(src, SCREENWIDTH, b, SCREENWIDTH, widths[w], rows);
                runs++;
                if (memcmp(a, b, sizeof a) && bad++ < 5)
                    fprintf(stderr, "C2PTEST box1 bad t %d groups %d rows %d\\n", t, widths[w], rows);
            }
    fprintf(stderr, "C2PTEST done, %d runs, %d mismatches\\n", runs, bad);
}
#endif
"""
p.write_text(s)
p = root / "src/i_main.c"
s = p.read_text()
s = s.replace("""	    if (!strcmp (myargv[k], "-mixtest"))
		bench_mixtest ();""", """	    if (!strcmp (myargv[k], "-mixtest"))
		bench_mixtest ();
	    else if (!strcmp (myargv[k], "-c2ptest"))
	    {
		extern void bench_c2ptest(void);
		bench_c2ptest ();
	    }""", 1)
assert "bench_c2ptest ()" in s
p.write_text(s)

# STDOOM (68000) has neither test: stubs, so that it links
p = root / "src/i_main.c"
s = p.read_text()
has_mix = "mix_first_channel" in (root / "src/i_sound.c").read_text()
has_pair = "c2p8_pairbox" in (root / "src/atari_c2p.c").read_text()
s += "\n#if !defined(__M68020__)%s\nvoid bench_mixtest(void) {}\n#endif\n" % ("" if has_mix else " || 1")
s += "#if !defined(TTDOOM)%s\nvoid bench_c2ptest(void) {}\n#endif\n" % ("" if has_pair else " || 1")
p.write_text(s)
print("patched", root)
