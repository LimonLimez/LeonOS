#ifndef LEONOS_LIBC_FENV_H
#define LEONOS_LIBC_FENV_H

#define FE_TONEAREST 0

typedef unsigned int fenv_t;
typedef unsigned int fexcept_t;

static inline int fegetround(void)
{
    return FE_TONEAREST;
}

static inline int fesetround(int mode)
{
    return mode == FE_TONEAREST ? 0 : -1;
}

#endif
