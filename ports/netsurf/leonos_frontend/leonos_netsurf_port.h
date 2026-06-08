#ifndef LEONOS_NETSURF_PORT_H
#define LEONOS_NETSURF_PORT_H

/*
 * Minimal LeonOS user ABI for the future NetSurf framebuffer frontend.
 *
 * This is not a libc, socket layer, or complete browser port. It reuses the
 * same freestanding syscall wrappers that are now QEMU-tested by UCDEMO.LEO.
 */

#include "../../../src32/include/leonos_user.h"

#define LEONOS_USER_VIRT_BASE 0x40000000u
#define LEONOS_USER_IMAGE_MAX (2u * 1024u * 1024u)

#endif
