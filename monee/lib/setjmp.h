#ifndef LIB_SETJMP_H
#define LIB_SETJMP_H

typedef int jmp_buf[8];

int setjmp(jmp_buf jmp_env);
void longjmp(jmp_buf jmp_env, int retval);

#endif
