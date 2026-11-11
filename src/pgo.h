//
// pgo.h: hooks for a profiling build (PGO=gen, see src/pgo.c). They're
// null unless pgo.c, which only that build links, sets them, and they're in
// every build so that the code profiled is the code the profile is used on.
//

#ifndef __PGO__
#define __PGO__

// main: started without arguments, take them from ARGS.TXT (Hatari can't
// pass arguments to a program it autostarts)
extern void (*pgo_args_hook) (int *argc, char ***argv);

// G_CheckDemoStatus: a timedemo has ended; write the profile
extern void (*pgo_dump_hook) (void);

#endif
