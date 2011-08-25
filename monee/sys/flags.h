#ifndef __FLAGS_H
#define __FLAGS_H

/* EFLAGS Register. */
#define FLAG_MBS  0x00000002    /* Must be set. */
#define FLAG_ZF   0x00000040    /* Interrupt Flag. */
#define FLAG_IF   0x00000200    /* Interrupt Flag. */
#define FLAG_DF   0x00000400    /* Direction Flag. */
#define FLAG_IOPL 0x00003000

#endif /* threads/flags.h */
