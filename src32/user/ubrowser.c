#include "leonos_user.h"

static const char default_url[] =
    "https://www.google.com/?igu=1&hl=en";

int leonos_user_main(void)
{
    leonos_write("LeonOS Browser launcher (NetSurf port track)\r\n");
    if (!leonos_browser_open(default_url)) {
        leonos_write("browser open rejected\r\n");
        return 1;
    }
    leonos_exit();
    return 0;
}
