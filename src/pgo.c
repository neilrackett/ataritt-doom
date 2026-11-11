//
// pgo.c: linked only into a TTDOOM built with PGO=gen (gcc 13), which
// records a profile for PGO=use (tools/bench/pgo.sh). Through the hooks in
// pgo.h it takes its arguments from ARGS.TXT when started without any, and
// writes the profile to PGO.DAT when a timedemo ends.
//
// gcc's own writer can't be used: at exit it writes a file per object at
// its path in the build (build/.../r_main.tt.gcda), which TOS's 8.3 names
// can't hold, and I_Error waits for a key before exiting. So the objects
// are built with -fprofile-info-section, and the profile is written as one
// stream of .gcda files, which gcov-tool merge-stream turns back into
// files beside the objects.
//

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <gcov.h>

#include "pgo.h"

// One pointer per object in the gcov_info section, between the start and
// stop symbols the linker provides for it
extern const struct gcov_info *const __start_gcov_info[];
extern const struct gcov_info *const __stop_gcov_info[];

static FILE		*pgo_file;
static unsigned long	pgo_bytes;

static void pgo_write (const void *data, unsigned n, void *arg)
{
    (void)arg;
    fwrite (data, 1, n, pgo_file);
    pgo_bytes += n;
}

static void pgo_filename (const char *name, void *arg)
{
    __gcov_filename_to_gcfn (name, pgo_write, arg);
}

static void *pgo_alloc (unsigned n, void *arg)
{
    (void)arg;
    return malloc (n);
}

static void pgo_dump (void)
{
    const struct gcov_info *const *info = __start_gcov_info;

    pgo_file = fopen ("PGO.DAT", "wb");
    if (!pgo_file)
    {
	fprintf (stderr, "PGO: can't create PGO.DAT\n");
	return;
    }
    // Hide where info starts, or gcc may assume two different arrays'
    // addresses never meet (gcc's own example does this)
    __asm__ ("" : "+r" (info));
    for ( ; info != __stop_gcov_info; info++)
	__gcov_info_to_gcda (*info, pgo_filename, pgo_write, pgo_alloc, NULL);
    fclose (pgo_file);
    fprintf (stderr, "PGO written: %lu bytes\n", pgo_bytes);
}

// The arguments, one line of ARGS.TXT split at spaces, e.g.
// "-timedemo demo1"
static void pgo_args (int *argc, char ***argv)
{
    static char	buf[256];
    static char	*args[32];
    FILE	*f;
    char	*t;
    int		n = 0;

    if (*argc > 1 || !(f = fopen ("ARGS.TXT", "r")))
	return;
    args[n++] = (*argv)[0];
    if (fgets (buf, sizeof buf, f))
	for (t = strtok (buf, " \t\r\n"); t && n < 31; t = strtok (NULL, " \t\r\n"))
	    args[n++] = t;
    fclose (f);
    args[n] = NULL;
    *argc = n;
    *argv = args;
}

__attribute__((constructor)) static void pgo_init (void)
{
    pgo_args_hook = pgo_args;
    pgo_dump_hook = pgo_dump;
}
