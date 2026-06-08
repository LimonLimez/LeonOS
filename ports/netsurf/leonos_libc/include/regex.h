#ifndef LEONOS_LIBC_REGEX_H
#define LEONOS_LIBC_REGEX_H

#include <stddef.h>

#define REG_EXTENDED 1
#define REG_ICASE 2
#define REG_NOSUB 4
#define REG_NEWLINE 8
#define REG_NOTBOL 1
#define REG_NOTEOL 2
#define REG_NOMATCH 1

typedef struct {
    int dummy;
} regex_t;

typedef struct {
    int rm_so;
    int rm_eo;
} regmatch_t;

int regcomp(regex_t *preg, const char *regex, int cflags);
int regexec(const regex_t *preg, const char *string, size_t nmatch,
            regmatch_t pmatch[], int eflags);
size_t regerror(int errcode, const regex_t *preg, char *errbuf,
                size_t errbuf_size);
void regfree(regex_t *preg);

#endif
