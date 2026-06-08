#ifndef LEONOS_LIBC_ARPA_INET_H
#define LEONOS_LIBC_ARPA_INET_H

#include <netinet/in.h>

int inet_aton(const char *cp, struct in_addr *inp);
int inet_pton(int af, const char *src, void *dst);

#endif
