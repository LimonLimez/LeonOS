#ifndef LEONOS_LIBC_SETJMP_H
#define LEONOS_LIBC_SETJMP_H

typedef unsigned int jmp_buf[6];

int setjmp(jmp_buf env);
void longjmp(jmp_buf env, int value);

#define _setjmp(env) setjmp(env)
#define _longjmp(env, value) longjmp((env), (value))

#endif
