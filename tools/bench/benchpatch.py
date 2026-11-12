#!/usr/bin/env python3
# benchpatch.py TREE: patches a copy of the tree for benchmarking (never the
# repo itself; mkbuild.sh makes the copy).
#
# - main() takes its arguments from ARGS.TXT when started without any:
#   Hatari can't pass arguments to an autostarted program.
# - -benchlog NAME: stdout and stderr go to that file (BENCH.TOS, ttbench.c,
#   passes RUNn.LOG): on a real TT (TOS 3.06) the GEMDOS redirection BENCH
#   set up before Pexec didn't reach the program, which wrote to the screen
#   and read the keyboard.
# - A demo ends at gametic 600, and prints "BENCH READY" (in main),
#   "BENCH START" (the first demo tic) and, at the end, "BENCH STATE": a hash
#   of every mobj's position, motion, angle, health and state and the random
#   index. Builds whose game logic is the same print the same hash.
# - I_Error (where every timedemo ends) doesn't wait for a key, so
#   BENCH.TOS runs on unattended.
# - "BENCH READS": the lumps read from the WAD while the demo was timed
#   (Doom doesn't precache a level for a demo), which cost no emulated time
#   under Hatari and real time on a TT.
# - -benchzone KB: a zone no bigger than that, as on a machine with less
#   free memory.
# - -benchtics N: a demo ends at tic N (default 600).
# - -playdemo NAME (real play: as many tics a frame as TryRunTics runs)
#   ends there too, with "played N gametics in R realtics, F frames".
#   -benchpace PCT runs the tic clock PCT% as fast: at 190, Hatari's TT has
#   a real TT's balance of tics to frames.
# - -benchfuzz SEED: ticcmds from a private generator, one tic a loop, god
#   mode, on whatever -warp names; a FUZZ line every 35 tics hashes the
#   game state in depth, and the run ends after -benchtics tics (fuzz.sh).
# - -noblit (TTDOOM): no C2P and no page flip (id parsed it "for
#   comparative timing purposes" and never used it): a run without it, less
#   one with it, is what the C2P costs.
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
s = s.replace("#include \"m_argv.h\"", """#include "m_argv.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
int bench_tics = 600;		// -benchtics N: where a demo or fuzz run ends
int bench_pace = 100;		// -benchpace PCT: the tic clock's speed
unsigned long bench_fuzz;	// -benchfuzz SEED: generated ticcmds
int bench_frames, bench_t0;	// frames drawn and the time since BENCH START
""", 1)
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
    {
	// -benchlog NAME: stdout and stderr to that file (BENCH.TOS). The
	// parent's GEMDOS redirection didn't reach the program on a real TT.
	int k;
	for (k = 1; k < argc - 1; k++)
	    if (!strcmp (argv[k], "-benchlog"))
	    {
		if (freopen (argv[k + 1], "w", stdout))
		    stderr = stdout;
		break;
	    }
	for (k = 1; k < argc - 1; k++)
	    if (!strcmp (argv[k], "-benchtics"))
		bench_tics = atoi (argv[k + 1]);
	    else if (!strcmp (argv[k], "-benchpace"))
		bench_pace = atoi (argv[k + 1]);
	    else if (!strcmp (argv[k], "-benchfuzz"))
	    {
		// one tic a loop, as a timedemo runs
		extern boolean singletics, nodrawers;
		int j;
		bench_fuzz = strtoul (argv[k + 1], NULL, 10) | 1;
		singletics = true;
		for (j = 1; j < argc; j++)
		    if (!strcmp (argv[j], "-nodraw"))
			nodrawers = true;
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
	extern int bench_frames, bench_t0;
	fprintf (stderr, "BENCH START\\n");
	fflush (stderr);
	bench_frames = 0;
	bench_t0 = I_GetTime ();
    }
    if (*demo_p == DEMOMARKER || gametic >= bench_tics)
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
    if (*demo_p == DEMOMARKER || gametic >= bench_tics) """, 1)
s = s.replace("void G_ReadDemoTiccmd (ticcmd_t* cmd) \n{", "extern int bench_tics;\nvoid G_ReadDemoTiccmd (ticcmd_t* cmd) \n{", 1)
assert "extern int bench_tics;\nvoid G_ReadDemoTiccmd" in s
p.write_text(s)

p = root / "src/i_video.c"
s = p.read_text()
# -noblit (TTDOOM; id parsed it and never used it): no C2P and no page
# flip, so a run without it, less one with it, is the C2P on any machine
old = "        since_last(bar, dirtybox, last_bar);"
assert old in s
s = s.replace(old, """        {
            extern boolean noblit;
            if (noblit)
                return;
        }
""" + old, 1)
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
p = root / "src/i_system.c"
s = p.read_text()
old = "    getchar();\n    exit(-1);"
assert old in s
s = s.replace(old, "    exit(-1);", 1)
# -benchzone KB: a zone no bigger than that, as on a machine with less
# free memory (a real TT's desk accessories and drivers)
old = "    unsigned long memavail = Malloc(-1);"
assert old in s
s = s.replace(old, """    {
	int p = M_CheckParm ("-benchzone");
	if (p && p < myargc - 1 && atoi (myargv[p + 1]) * 1024 < *size)
	    *size = atoi (myargv[p + 1]) * 1024;
    }
""" + old, 1)
p.write_text(s)

# BENCH READS: the lumps read from the WAD while the demo is timed. Doom
# doesn't precache a level for a demo, so a timedemo reads each texture,
# flat and sprite when it first comes into view, and again after the zone
# purges it: no time under Hatari's drive emulation, real time on a TT.
p = root / "src/w_wad.c"
s = p.read_text()
old = "    lseek (handle, l->position, SEEK_SET);\n    c = read (handle, dest, l->size);\n"
assert old in s
s = s.replace(old, old + "    bench_reads++;\n    bench_readbytes += l->size;\n", 1)
old = "\nvoid\nW_ReadLump\n("
assert s.count(old) == 1
s = s.replace(old, "\nint bench_reads, bench_readbytes;\n" + old, 1)
p.write_text(s)
p = root / "src/g_game.c"
s = p.read_text()
old = "    starttime = I_GetTime (); \n"
assert old in s
s = s.replace(old, old + "    { extern int bench_reads, bench_readbytes; bench_reads = bench_readbytes = 0; }\n", 1)
old = """	fprintf (stderr, "BENCH STATE tic %d mobjs %d rnd %d hash %08lx\\n",
		 gametic, n, prndindex, h);
"""
assert old in s
s = s.replace(old, old + """	{
	    extern int bench_reads, bench_readbytes;
	    fprintf (stderr, "BENCH READS %d lumps, %d bytes\\n",
		     bench_reads, bench_readbytes);
	}
	if (!timingdemo)
	{
	    // -playdemo: real play, as many tics a frame as TryRunTics runs
	    extern int bench_frames, bench_t0;
	    I_Error ("played %i gametics in %i realtics, %i frames",
		     gametic, I_GetTime () - bench_t0, bench_frames);
	}
""", 1)

# -benchfuzz SEED: ticcmds from a private generator instead of the keyboard,
# one tic a loop (main sets singletics), god mode and the shareware weapons,
# so every build plays the same game on any map: FUZZ lines give a hash of
# the game state every 35 tics, far more of it than BENCH STATE's.
old = "void G_BuildTiccmd (ticcmd_t* cmd) \n{"
assert s.count(old) == 1
s = s.replace(old, """#include "p_local.h"
#include "r_state.h"
extern unsigned long bench_fuzz;
static unsigned long bench_fz;
static int bench_fztic;

static unsigned bench_rnd (void)
{
    bench_fz = bench_fz * 1103515245u + 12345u;
    return (bench_fz >> 16) & 0x7fff;
}

static unsigned long bench_deephash (void)
{
    extern int prndindex;
    player_t *p = &players[consoleplayer];
    thinker_t *th;
    unsigned long h = 2166136261u;
    int i;
#define BH(v) h = (h ^ (unsigned long)(v)) * 16777619u
    for (th = thinkercap.next; th != &thinkercap; th = th->next)
    {
	mobj_t *mo;
	if (th->function.acp1 != (actionf_p1)P_MobjThinker)
	{
	    BH (0x5a5a);
	    continue;
	}
	mo = (mobj_t *)th;
	BH (mo->x); BH (mo->y); BH (mo->z);
	BH (mo->momx); BH (mo->momy); BH (mo->momz);
	BH (mo->angle); BH (mo->health); BH (mo->state - states);
	BH (mo->tics); BH (mo->flags); BH (mo->floorz); BH (mo->ceilingz);
	BH (mo->movedir); BH (mo->movecount); BH (mo->reactiontime);
	BH (mo->threshold); BH (mo->type); BH (mo->radius); BH (mo->height);
	BH (mo->target ? mo->target->x ^ mo->target->type : -1);
	BH (mo->subsector - subsectors);
    }
    for (i = 0; i < numsectors; i++)
    {
	BH (sectors[i].floorheight); BH (sectors[i].ceilingheight);
	BH (sectors[i].lightlevel); BH (sectors[i].special);
	BH (sectors[i].floorpic); BH (sectors[i].ceilingpic);
	BH (sectors[i].soundtarget ? sectors[i].soundtarget->x : -1);
    }
    for (i = 0; i < numlines; i++)
	BH (lines[i].special);
    BH (p->health); BH (p->armorpoints); BH (p->readyweapon);
    BH (p->pendingweapon); BH (p->killcount); BH (p->itemcount);
    BH (p->secretcount); BH (p->damagecount); BH (p->bonuscount);
    BH (p->viewz); BH (p->playerstate); BH (p->refire);
    for (i = 0; i < NUMAMMO; i++)
	BH (p->ammo[i]);
    BH (prndindex); BH (leveltime); BH (gamemap);
#undef BH
    return h;
}

static void bench_fuzzcmd (ticcmd_t* cmd)
{
    static const signed char fwds[8] = { 50, 50, 50, 50, 25, 25, 0, -25 };
    static const signed char sides[8] = { 0, 0, 0, 0, 40, -40, 24, -24 };
    static const short turns[8] = { 0, 0, 0, 320, -320, 1280, -1280, 640 };
    static const char weapons[6] = { wp_fist, wp_pistol, wp_shotgun,
				     wp_chaingun, wp_missile, wp_chainsaw };
    static int hold, fwd, side, turn, attack;
    extern int bench_tics;
    player_t *p = &players[consoleplayer];
    int i, once = 0;

    if (!bench_fztic)
	bench_fz = bench_fuzz;
    if (gamestate == GS_LEVEL && p->mo)
    {
	// the shareware weapons only: its WAD has no plasma or BFG sprites
	p->cheats |= CF_GODMODE;
	for (i = 0; i < 6; i++)
	    p->weaponowned[(int)weapons[i]] = true;
	p->ammo[am_clip] = p->maxammo[am_clip];
	p->ammo[am_shell] = p->maxammo[am_shell];
	p->ammo[am_misl] = p->maxammo[am_misl];
    }
    if (hold-- <= 0)
    {
	unsigned r = bench_rnd ();
	hold = 5 + bench_rnd () % 30;
	fwd = fwds[r & 7];
	side = sides[(r >> 3) & 7];
	turn = turns[(r >> 6) & 7];
	attack = ((r >> 9) & 7) < 3;
	// for this tic only: use (a new press each time) or a weapon
	if ((r >> 12) & 1)
	    once = BT_USE;
	else if (((r >> 13) & 3) == 0)
	    once = BT_CHANGE
		| (weapons[bench_rnd () % 6] << BT_WEAPONSHIFT);
    }
    cmd->forwardmove = fwd;
    cmd->sidemove = side;
    cmd->angleturn = turn;
    cmd->buttons = (attack ? BT_ATTACK : 0) | once;

    bench_fztic++;
    if (gamestate == GS_LEVEL && !(bench_fztic % 35))
	fprintf (stderr, "FUZZ %d map %d hash %08lx\\n", bench_fztic,
		 gamemap, bench_deephash ());
    if (bench_fztic >= bench_tics)
    {
	fflush (stderr);
	I_Error ("fuzzed %i tics", bench_fztic);
    }
}

""" + old, 1)
import re
m = re.search(r"    cmd->consistancy = *\n\s*consistancy\[consoleplayer\]\[maketic%BACKUPTICS\]; *\n", s)
assert m
s = s[:m.end()] + "    if (bench_fuzz)\n    {\n\tbench_fuzzcmd (cmd);\n\treturn;\n    }\n" + s[m.end():]
p.write_text(s)

# No NetUpdate in a fuzz run: called from the renderer it would build
# ticcmds whenever the clock moved, and the generator must run once a tic
p = root / "src/d_net.c"
s = p.read_text()
old = "    // check time\n    nowtime = I_GetTime ()/ticdup;"
assert old in s
s = s.replace(old, "    { extern unsigned long bench_fuzz; if (bench_fuzz) return; }\n" + old, 1)
p.write_text(s)

# Frames drawn, for -playdemo's result
p = root / "src/i_video.c"
s = p.read_text()
old = "void I_FinishUpdate (void)\n{\n"
assert old in s
s = s.replace(old, old + "    { extern int bench_frames; bench_frames++; }\n", 1)
p.write_text(s)

# -benchpace PCT: the tic clock PCT% as fast. Hatari's TT runs TTDOOM about
# 1.9 times as fast as a real one, so at 190 a -playdemo run has the real
# TT's balance of tics to frames (and its slow motion below 7 fps).
p = root / "src/i_system.c"
s = p.read_text()
old = "    return (t-basetime)*TICRATE/200;"
assert old in s
s = s.replace(old, """    {
	extern int bench_pace;
	if (bench_pace != 100)
	    return (t-basetime)*(unsigned long)(TICRATE*bench_pace)/20000;
    }
""" + old, 1)
p.write_text(s)

# -rcheck: every 3D view drawn twice, by the old code and the new, and
# compared. While a renderer change is being made its old code stays in
# the source under R_CHECK, chosen by r_oldpath; RCHECK=1 (the environment
# of mkbuild.sh) defines R_CHECK. Frames differ between builds by a few
# layout-dependent reads, so only one binary can compare exactly. Without
# R_CHECK the two passes are the same code: zero differences, which checks
# the check.
import os
if os.environ.get("RCHECK"):
    p = root / "src/doomdef.h"
    s = p.read_text()
    old = "#ifndef __DOOMDEF__\n#define __DOOMDEF__\n"
    assert old in s
    s = s.replace(old, old + "#define R_CHECK 1\n", 1)
    p.write_text(s)
p = root / "src/r_main.c"
s = p.read_text()
old = "void R_RenderPlayerView (player_t* player)\n{"
assert s.count(old) == 1
s = s.replace(old, "static void R_RenderPlayerView1 (player_t* player)\n{", 1)
s += """
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "m_argv.h"
#include "v_video.h"
int r_oldpath;
static int rcheck_on = -1, rcheck_frames, rcheck_badframes, rcheck_firsttic = -1;
static long rcheck_pixels;

void R_RenderPlayerView (player_t* player)
{
    static byte *save;
    extern short fuzzpos;
    extern int gametic;
    int fuzz, i, bad = 0;

    if (rcheck_on < 0)
	rcheck_on = M_CheckParm ("-rcheck") != 0;
    if (!rcheck_on)
    {
	R_RenderPlayerView1 (player);
	return;
    }
    if (!save)
	save = malloc (SCREENWIDTH*SCREENHEIGHT);
    fuzz = fuzzpos;
    r_oldpath = 1;
    R_RenderPlayerView1 (player);
    memcpy (save, screens[0], SCREENWIDTH*SCREENHEIGHT);
    fuzzpos = fuzz;
    r_oldpath = 0;
    R_RenderPlayerView1 (player);
    for (i = 0; i < SCREENWIDTH*SCREENHEIGHT; i++)
	if (save[i] != screens[0][i])
	    bad++;
    rcheck_frames++;
    if (bad)
    {
	if (rcheck_firsttic < 0)
	{
	    rcheck_firsttic = gametic;
	    for (i = 0; save[i] == screens[0][i]; i++)
		;
	    fprintf (stderr, "RCHECK first difference: tic %d, x %d y %d, %d pixels\\n",
		     gametic, i % SCREENWIDTH, i / SCREENWIDTH, bad);
	}
	rcheck_badframes++;
	rcheck_pixels += bad;
    }
}

void bench_rcheck_report (void)
{
    if (rcheck_on > 0)
	fprintf (stderr, "RCHECK frames %d, %d differ, %ld pixels\\n",
		 rcheck_frames, rcheck_badframes, rcheck_pixels);
}
"""
p.write_text(s)
p = root / "src/g_game.c"
s = p.read_text()
old = """	if (!timingdemo)
	{
	    // -playdemo: real play, as many tics a frame as TryRunTics runs"""
assert old in s
s = s.replace(old, """	{
	    extern void bench_rcheck_report (void);
	    bench_rcheck_report ();
	}
""" + old, 1)
old = """    if (bench_fztic >= bench_tics)
    {
	fflush (stderr);"""
assert old in s
s = s.replace(old, """    if (bench_fztic >= bench_tics)
    {
	extern void bench_rcheck_report (void);
	bench_rcheck_report ();
	fflush (stderr);""", 1)
p.write_text(s)
print("patched", root)
