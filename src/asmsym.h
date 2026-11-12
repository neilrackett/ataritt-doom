// Top-level asm's view of C: a C symbol's name as the assembler has it
// (a.out puts an underscore before it, ELF nothing), and the calling
// convention asm that reads its arguments from the stack needs.

#ifndef __ASMSYM__
#define __ASMSYM__

#define ASM_STR2(x) #x
#define ASM_STR(x) ASM_STR2(x)
#define ASM_SYM(name) ASM_STR(__USER_LABEL_PREFIX__) #name

#ifdef __FASTCALL__
#define ASM_CDECL __attribute__((cdecl))
#else
#define ASM_CDECL
#endif

#endif
