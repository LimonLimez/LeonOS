#include "leonos_user.h"

extern int main(int argc, char **argv);

#if defined(__has_include)
#if __has_include("leonos_netsurf_config.h")
#include "leonos_netsurf_config.h"
#endif
#endif

#ifndef LEONOS_NETSURF_DEFAULT_JAVASCRIPT
#define LEONOS_NETSURF_DEFAULT_JAVASCRIPT 0
#endif

int leonos_user_main(void)
{
    static char arg0[] = "netsurf-monkey";
    static char arg1[] =
#if LEONOS_NETSURF_DEFAULT_JAVASCRIPT
        "--enable_javascript=1";
#else
        "--enable_javascript=0";
#endif
    static char arg2[] = "--script_timeout=5";
    static char *argv[] = { arg0, arg1, arg2, 0 };

    leonos_write("NETSURF.LEO NetSurf monkey frontend starting\r\n");
    int rc = main(3, argv);
    leonos_write("NETSURF.LEO NetSurf monkey frontend returned\r\n");
    return rc;
}
