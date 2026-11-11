/* BENCH.TOS: runs the timedemos in BENCH.TXT one after another, unattended,
 * and collects the results in RESULTS.TXT: timing TTDOOM on a real TT
 * (ttkit.sh builds it into the kit). Any 68000 or later, plain TOS.
 *
 * BENCH.TXT: a run a line, [+N] PROGRAM PARAMETERS [# NOTE]; a line
 * starting with '#' is skipped. A note "Hatari R" (the realtics Hatari
 * gave) adds the ratio of the real time to it.
 *
 * Where a program loads matters: GEMDOS keeps blocks only even, and
 * TTDOOM loaded 2 bytes off a long ran 2-4% slower under Hatari (its
 * static data straddling longs). So each program's text is loaded on a
 * 16-byte boundary, or N bytes past one with +N: a pad block in front of
 * the largest free block (TT-RAM if there is any), where TOS puts the
 * copy of the environment and then the program. The environment's size
 * varies from machine to machine, so the program is first loaded without
 * running it (Pexec mode 3) to see where it lands, and the pad corrected.
 *
 * Each run's output goes to RUNn.LOG: stdout and stderr are redirected to
 * it, and stdin to an empty file, so I_Error's getchar() after "timed N
 * gametics in R realtics" returns at once and the next run starts.
 * RESULTS.TXT is appended to and closed after every run, so a crash keeps
 * the runs before it.
 */
#include <mint/osbind.h>
#include <mint/basepage.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAXRUNS 32
#define LINE 128

static void trim(char *s)
{
    size_t n;

    s[strcspn(s, "\r\n")] = 0;
    n = strlen(s);
    while (n && (s[n - 1] == ' ' || s[n - 1] == '\t'))
        s[--n] = 0;
}

static char *skip(char *s)
{
    while (*s == ' ' || *s == '\t')
        s++;
    return s;
}

static void copy(char *d, const char *s, size_t n)
{
    strncpy(d, s, n - 1);
    d[n - 1] = 0;
}

/* The run's result and two start-up lines, from its log */
static void scan_log(const char *name, long *gt, long *rt, char *caches,
                     char *mem)
{
    char line[256], *p;
    FILE *f = fopen(name, "r");

    *gt = *rt = -1;
    caches[0] = mem[0] = 0;
    if (!f)
        return;
    while (fgets(line, sizeof line, f))
    {
        trim(line);
        if ((p = strstr(line, "timed ")) != NULL)
            sscanf(p, "timed %ld gametics in %ld realtics", gt, rt);
        else if ((p = strstr(line, "68030 caches")) != NULL)
            copy(caches, p, LINE);
        else if (strstr(line, "allocated at"))
            copy(mem, line, LINE);
    }
    fclose(f);
}

/* Make the largest free block, where the next program loads, start N
 * bytes past a 16-byte boundary: take that block, then shrink it to the
 * pad in front. Returns the pad block (to free after the run), or 0. */
static long pad_tpa(int n)
{
    int mode = Mxalloc(-1, 1) > 0 ? 1 : 0;
    long size = Mxalloc(-1, mode), a, pad;

    if (size <= 0 || (a = Mxalloc(size, mode)) <= 0)
        return 0;
    pad = (n - a) & 15;
    if (!pad)
    {
        Mfree((void *)a);
        return 0;
    }
    Mshrink((void *)a, pad);
    return a;
}

/* Run FILE with its text N bytes past a 16-byte boundary: load it without
 * running it to see where the pad puts it, correct the pad, and run it.
 * Returns Pexec's result; *at, where the last trial load put the text. */
static long run_aligned(const char *file, char *tail, int n, long *at)
{
    int want = n, i;
    long pad;

    *at = 0;
    for (i = 0; i < 3; i++)
    {
        BASEPAGE *bp;
        long miss;

        pad = pad_tpa(want);
        bp = (BASEPAGE *)Pexec(3, file, tail, NULL);
        if ((long)bp < 0)
        {
            if (pad)
                Mfree((void *)pad);
            return (long)bp;
        }
        *at = (long)bp->p_tbase;
        miss = (*at - n) & 15;
        Mfree(bp->p_env);
        Mfree(bp);
        if (pad)
            Mfree((void *)pad);
        if (!miss)
            break;
        want = (want - (int)miss) & 15;
    }
    pad = pad_tpa(want);
    {
        long r = Pexec(0, file, tail, NULL);

        if (pad)
            Mfree((void *)pad);
        return r;
    }
}

/* To the screen and RESULTS.TXT */
static void report(const char *s)
{
    FILE *f = fopen("RESULTS.TXT", "a");

    printf("%s\n", s);
    if (f)
    {
        fprintf(f, "%s\r\n", s);
        fclose(f);
    }
}

int main(void)
{
    static char runs[MAXRUNS][LINE];
    char buf[LINE * 2], line[LINE * 3], prog[LINE], file[LINE], note[LINE];
    char tail[128], logname[16], caches[LINE], mem[LINE];
    int nruns = 0, i;
    FILE *f = fopen("BENCH.TXT", "r");

    if (!f)
    {
        printf("BENCH: no BENCH.TXT here. Press a key.\n");
        Cnecin();
        return 1;
    }
    while (nruns < MAXRUNS && fgets(buf, sizeof buf, f))
    {
        trim(buf);
        if (buf[0] && buf[0] != '#')
            copy(runs[nruns++], buf, LINE);
    }
    fclose(f);

    {
        long e = Fcreate("EMPTY.TXT", 0);

        if (e >= 0)
            Fclose((short)e);
    }
    sprintf(line, "BENCH: %d runs", nruns);
    report(line);

    for (i = 0; i < nruns; i++)
    {
        char *args, *p;
        long lh, in, h0, h1, h2, r, gt, rt, at;
        int off = 0;
        size_t n;

        /* [+N] PROGRAM PARAMETERS [# NOTE] */
        note[0] = 0;
        if ((p = strchr(runs[i], '#')) != NULL)
        {
            copy(note, skip(p + 1), LINE);
            *p = 0;
            trim(runs[i]);
        }
        args = skip(runs[i]);
        if (*args == '+')
        {
            off = (int)strtol(args + 1, &args, 10) & 14;
            args = skip(args);
        }
        n = strcspn(args, " \t");
        copy(prog, args, n + 1 < LINE ? n + 1 : LINE);
        args = skip(args + n);
        n = strlen(args);
        if (n > 124)
            n = 124;
        tail[0] = (char)n;
        memcpy(tail + 1, args, n);
        tail[n + 1] = 0;

        /* The program's name in the results: +N for one loaded off */
        copy(file, prog, LINE);
        if (off)
        {
            sprintf(line, "+%d %s", off, prog);
            copy(prog, line, LINE);
        }

        sprintf(logname, "RUN%d.LOG", i + 1);
        printf("BENCH %d/%d: %s %s\n", i + 1, nruns, prog, args);
        fflush(stdout);

        lh = Fcreate(logname, 0);
        in = Fopen("EMPTY.TXT", 0);
        if (lh < 0 || in < 0)
        {
            sprintf(line, "%-2d %s %s: can't open %s or EMPTY.TXT",
                    i + 1, prog, args, logname);
            report(line);
            continue;
        }
        h0 = Fdup(0);
        h1 = Fdup(1);
        h2 = Fdup(2);
        Fforce(0, (short)in);
        Fforce(1, (short)lh);
        Fforce(2, (short)lh);
        r = run_aligned(file, tail, off, &at);
        Fforce(0, (short)h0);
        Fforce(1, (short)h1);
        Fforce(2, (short)h2);
        Fclose((short)h0);
        Fclose((short)h1);
        Fclose((short)h2);
        Fclose((short)in);
        Fclose((short)lh);

        scan_log(logname, &gt, &rt, caches, mem);
        if (gt > 0 && rt > 0)
        {
            /* ms a frame and fps, to one and two decimals */
            long ms10 = (rt * 10000L / 35 + gt / 2) / gt;
            long fps100 = (gt * 3500L + rt / 2) / rt;

            sprintf(line, "%-2d %s %s: %ld realtics, %ld tics, "
                    "%ld.%ld ms, %ld.%02ld fps", i + 1, prog, args, rt, gt,
                    ms10 / 10, ms10 % 10, fps100 / 100, fps100 % 100);
            if (!strncmp(note, "Hatari ", 7) && atol(note + 7) > 0)
            {
                long ratio = (rt * 1000L + atol(note + 7) / 2)
                    / atol(note + 7);

                sprintf(line + strlen(line), " (%s, real/Hatari %ld.%03ld)",
                        note, ratio / 1000, ratio % 1000);
            }
            else if (note[0])
                sprintf(line + strlen(line), " (%s)", note);
        }
        else
            sprintf(line, "%-2d %s %s: no result (exit %ld), see %s",
                    i + 1, prog, args, r, logname);
        report(line);
        if (caches[0])
        {
            sprintf(line, "     %s", caches);
            report(line);
        }
        if (mem[0])
        {
            sprintf(line, "     %s", mem);
            report(line);
        }
    }

    printf("BENCH: done, results in RESULTS.TXT. Press a key.\n");
    Cnecin();
    return 0;
}
